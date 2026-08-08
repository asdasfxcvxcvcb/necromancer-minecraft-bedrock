#include "pch.h"
#include "ChestStealer.h"

#include "client/event/events/RenderLayerEvent.h"
#include "client/event/events/TickEvent.h"
#include "mc/Addresses.h"
#include "mc/common/client/gui/controls/VisualTree.h"
#include "mc/common/client/gui/controls/UIControl.h"
#include "mc/common/client/gui/screens/ContainerScreenController.h"
#include "mc/common/world/ItemStack.h"
#include "util/Logger.h"
#include "util/memory.h"

namespace {
    const std::string COLL_CONTAINER = "container_items";

    constexpr int MAX_ACTIONS_PER_TICK = 8;
    constexpr int MAX_SLOT_ATTEMPTS = 3;

    std::string itemIdOf(SDK::ItemStack* stack) {
        auto item = stack->getItem();
        if (!item) return {};
        return item->namespacedId.getString();
    }
}

ChestStealer::ChestStealer()
    : Module("ChestStealer", LocalizeString::get("client.module.chestStealer.name"),
             LocalizeString::get("client.module.chestStealer.desc"), GAME, nokeybind) {
    listen<RenderLayerEvent>(static_cast<EventListenerFunc>(&ChestStealer::onRenderLayer));
    listen<TickEvent>(static_cast<EventListenerFunc>(&ChestStealer::onTick));

    addSliderSetting("time", LocalizeString::get("client.module.chestStealer.time.name"),
                     LocalizeString::get("client.module.chestStealer.time.desc"), time, FloatValue(0.01f),
                     FloatValue(10.f), FloatValue(0.01f));
    addSetting("closeAfterLoot", LocalizeString::get("client.module.chestStealer.closeAfterLoot.name"),
               LocalizeString::get("client.module.chestStealer.closeAfterLoot.desc"), closeAfterLoot);
}

void ChestStealer::resetSession() {
    controller = nullptr;
    containerScreen = false;
    phase = Phase::Scan;
    plan.clear();
    planCursor = 0;
    lootedStacks = 0;
    verifySlot = -1;
    verifyCount = 0;
    verifyAttempts = 0;
    exitRequested = false;
    sessionStart = {};
}

void ChestStealer::onEnable() {
    resetSession();
}

void ChestStealer::onDisable() { resetSession(); }

SDK::ItemStack* ChestStealer::readSlot(const std::string& collection, int slot) {
    if (!controller) return nullptr;
    auto stack = controller->getItemStack(collection, slot);
    if (!stack || !stack->valid || stack->itemCount == 0) return nullptr;
    if (!stack->getItem()) return nullptr;
    return stack;
}

void ChestStealer::onRenderLayer(Event& evG) {
    auto& ev = reinterpret_cast<RenderLayerEvent&>(evG);
    auto view = ev.getScreenView();
    if (!view || !view->visualTree || !view->visualTree->rootControl) return;

    auto& name = view->visualTree->rootControl->name;
    bool isContainer = name.find("chest") != std::string::npos || name.find("barrel") != std::string::npos ||
                       name.find("shulker") != std::string::npos || name.find("hopper") != std::string::npos ||
                       name.find("dispenser") != std::string::npos || name.find("dropper") != std::string::npos;
    bool isInventory = name == "inventory_screen";
    if (!isContainer && !isInventory) return;

    auto now = std::chrono::steady_clock::now();
    auto newController = reinterpret_cast<SDK::ContainerScreenController*>(view->screenController);
    if (!newController) return;

    if (newController != controller || now - lastSeen > 750ms) {
        resetSession();
        controller = newController;
        sessionStart = now;
        containerScreen = isContainer;
        auto vtable = *reinterpret_cast<void***>(controller);
        auto mgrVtable = controller->containerManager ? *reinterpret_cast<void***>(controller->containerManager) : nullptr;
        bool canUse = memory::callVirtual<bool>(controller, 39);
    }
    lastSeen = now;
}

std::chrono::steady_clock::duration ChestStealer::perActionDelay() const {
    float budget = std::get<FloatValue>(time).value;
    if (budget < 0.01f) budget = 0.01f;
    int steps = static_cast<int>(plan.size());
    if (steps < 1) steps = 1;
    return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<float>(budget / steps));
}

void ChestStealer::onTick(Event&) {
    if (!controller) return;
    auto now = std::chrono::steady_clock::now();
    if (now - lastSeen > 750ms) {
        resetSession();
        return;
    }
    if (now - sessionStart < 250ms) return;
    if (phase == Phase::Done) return;

    auto delay = perActionDelay();
    if (phase != Phase::Scan && now - lastAction < delay) return;

    int budget = 1;
    if (phase != Phase::Scan && delay > std::chrono::steady_clock::duration::zero()) {
        auto elapsed = now - lastAction;
        auto cap = delay * MAX_ACTIONS_PER_TICK;
        if (elapsed > cap) elapsed = cap;
        budget = static_cast<int>(elapsed / delay);
        if (budget < 1) budget = 1;
    }

    bool acted = false;
    for (int i = 0; i < budget; i++) {
        if (!processAction(now)) break;
        acted = true;
        if (phase == Phase::Done) break;
    }
    if (acted) lastAction = now;
}

bool ChestStealer::processAction(std::chrono::steady_clock::time_point now) {
    switch (phase) {
    case Phase::Scan:
        return buildPlan(now);
    case Phase::Steal:
        return processSteal();
    case Phase::Done:
        return false;
    }
    return false;
}

bool ChestStealer::buildPlan(std::chrono::steady_clock::time_point now) {
    if (!containerScreen) {
        finish();
        return false;
    }

    int found = 0;
    std::string dump;
    for (int i = 0; i < 54; i++) {
        auto stack = readSlot(COLL_CONTAINER, i);
        if (!stack) continue;
        found++;
        if (dump.size() < 160) {
            if (!dump.empty()) dump += ", ";
            dump += std::to_string(i) + ":" + itemIdOf(stack) + "x" + std::to_string(stack->itemCount);
        }
        plan.push_back(i);
    }

    if (found == 0 && now - sessionStart < 1500ms) {
        plan.clear();
        return false;
    }


    lastAction = now;
    if (plan.empty()) {
        finish();
    } else {
        phase = Phase::Steal;
    }
    return false;
}

bool ChestStealer::processSteal() {
    if (verifySlot >= 0) {
        auto stack = readSlot(COLL_CONTAINER, verifySlot);
        int left = stack ? stack->itemCount : 0;
        if (left > 0 && left < verifyCount && verifyAttempts < MAX_SLOT_ATTEMPTS) {
            verifyCount = left;
            verifyAttempts++;
            controller->autoPlaceSlot(COLL_CONTAINER, verifySlot, left);
            return true;
        }
        if (left > 0) {
            Logger::Warn("[ChestStealer] slot {} still holds {} after {} attempts, skipping", verifySlot, left,
                         verifyAttempts + 1);
        } else {
            lootedStacks++;
        }
        verifySlot = -1;
        verifyAttempts = 0;
    }

    while (planCursor < plan.size()) {
        int slot = plan[planCursor++];
        auto stack = readSlot(COLL_CONTAINER, slot);
        if (!stack) continue;
        verifySlot = slot;
        verifyCount = stack->itemCount;
        verifyAttempts = 0;
        controller->autoPlaceSlot(COLL_CONTAINER, slot, verifyCount);
        return true;
    }

    finish();
    return false;
}

void ChestStealer::finish() {
    phase = Phase::Done;
    if (!std::get<BoolValue>(closeAfterLoot) || exitRequested) return;
    if (!containerScreen || !controller || lootedStacks <= 0) return;
    exitRequested = true;
    controller->tryExit();
}
