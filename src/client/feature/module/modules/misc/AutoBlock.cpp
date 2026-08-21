#include "pch.h"
#include "AutoBlock.h"

#include "client/Necromancer.h"
#include "client/event/events/TickEvent.h"
#include "client/feature/module/modules/misc/LegitScaffold.h"
#include "client/screen/ScreenManager.h"
#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/player/LocalPlayer.h"
#include "mc/common/world/actor/player/PlayerInventory.h"
#include "mc/common/world/actor/player/Inventory.h"
#include "mc/common/world/ItemStack.h"
#include <unordered_set>
#include <string>

namespace {
    bool isTntItem(std::string const& id) {
        return id == "minecraft:tnt" || id == "minecraft:tnt_minecart";
    }

    bool isUnsafeItem(std::string const& id) {
        static const std::unordered_set<std::string> unsafe = {
            "minecraft:web",           "minecraft:cobweb",        "minecraft:fire",
            "minecraft:soul_fire",     "minecraft:lava",          "minecraft:flowing_lava",
            "minecraft:water",         "minecraft:flowing_water", "minecraft:magma",
            "minecraft:campfire",      "minecraft:soul_campfire", "minecraft:cactus",
            "minecraft:powder_snow",   "minecraft:sweet_berry_bush",
        };
        return unsafe.contains(id);
    }

    bool isPlaceable(SDK::ItemStack* stack, bool ignoreTnt) {
        if (!stack || !stack->block) return false;
        auto* item = stack->getItem();
        if (!item) return false;

        std::string id = item->namespacedId.getString();
        if (ignoreTnt && isTntItem(id)) return false;
        return !isUnsafeItem(id);
    }
}

AutoBlock::AutoBlock()
    : Module("AutoBlock", LocalizeString::get("client.module.autoBlock.name"),
             LocalizeString::get("client.module.autoBlock.desc"), GAME, nokeybind) {
    listen<TickEvent>(static_cast<EventListenerFunc>(&AutoBlock::onTick));

    addSliderSetting("threshold", LocalizeString::get("client.module.autoBlock.threshold.name"),
                     LocalizeString::get("client.module.autoBlock.threshold.desc"), threshold, FloatValue(0.f),
                     FloatValue(5.f), FloatValue(1.f));
    addSetting("sameBlockOnly", LocalizeString::get("client.module.autoBlock.sameBlockOnly.name"),
               LocalizeString::get("client.module.autoBlock.sameBlockOnly.desc"), sameBlockOnly);
    addSetting("ignoreTnt", LocalizeString::get("client.module.autoBlock.ignoreTnt.name"),
               LocalizeString::get("client.module.autoBlock.ignoreTnt.desc"), ignoreTnt);
    addSetting("scaffoldAware", LocalizeString::get("client.module.autoBlock.scaffoldAware.name"),
               LocalizeString::get("client.module.autoBlock.scaffoldAware.desc"), scaffoldAware);
}

int AutoBlock::scaffoldHeadroom() {
    if (!std::get<BoolValue>(scaffoldAware).value) return 0;

    auto mod = Necromancer::getModuleManager().find("LegitScaffold");
    if (!mod || !mod->isEnabled()) return 0;

    return static_cast<LegitScaffold*>(mod.get())->blocksPerPlacement();
}

void AutoBlock::onTick(Event&) {
    auto ci = SDK::ClientInstance::get();
    auto plr = ci ? ci->getLocalPlayer() : nullptr;
    if (!plr || !plr->supplies || !plr->supplies->inventory) {
        lastBlock = nullptr;
        lastSlot = -1;
        return;
    }
    if (Necromancer::get().getScreenManager().getActiveScreen().has_value()) return;

    auto inv = plr->supplies->inventory;
    int sel = plr->supplies->selectedSlot;
    if (sel < 0 || sel >= 9) {
        lastBlock = nullptr;
        lastSlot = -1;
        return;
    }

    int thr = static_cast<int>(std::get<FloatValue>(threshold).value) + scaffoldHeadroom();
    bool sameOnly = std::get<BoolValue>(sameBlockOnly);
    bool skipTnt = std::get<BoolValue>(ignoreTnt);

    auto held = inv->getItem(sel);

    if (held && held->itemCount > 0 && isPlaceable(held, skipTnt)) {
        if (held->itemCount <= thr) {
            int slot = findRefillSlot(inv, sel, held->block, thr + 1, sameOnly, skipTnt);
            if (slot >= 0) {
                plr->supplies->selectedSlot = slot;
                lastBlock = nullptr;
                lastSlot = -1;
                return;
            }
        }
        lastBlock = held->block;
        lastSlot = sel;
        return;
    }

    if (!held || held->itemCount <= 0) {
        if (lastBlock && sel == lastSlot) {
            int slot = findRefillSlot(inv, sel, lastBlock, 1, sameOnly, skipTnt);
            if (slot >= 0) plr->supplies->selectedSlot = slot;
        }
    }
    lastBlock = nullptr;
    lastSlot = -1;
}

int AutoBlock::findRefillSlot(SDK::Inventory* inv, int currentSlot, SDK::Block* wanted, int minCount, bool sameOnly,
                              bool skipTnt) const {
    int fallback = -1;
    for (int i = 0; i < 9; i++) {
        if (i == currentSlot) continue;
        auto stack = inv->getItem(i);
        if (!isPlaceable(stack, skipTnt)) continue;
        if (stack->itemCount < minCount) continue;
        if (wanted && stack->block == wanted) return i;
        if (!sameOnly && fallback < 0) fallback = i;
    }
    return sameOnly ? -1 : fallback;
}

void AutoBlock::onDisable() {
    lastBlock = nullptr;
    lastSlot = -1;
}
