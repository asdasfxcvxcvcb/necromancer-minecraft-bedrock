#include "pch.h"
#include "ChestStealer.h"

#include "client/event/events/RenderLayerEvent.h"
#include "client/event/events/TickEvent.h"
#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/gui/controls/VisualTree.h"
#include "mc/common/client/gui/controls/UIControl.h"
#include "mc/common/client/gui/screens/ContainerScreenController.h"
#include "mc/common/client/player/LocalPlayer.h"
#include "mc/common/world/ItemStack.h"
#include "util/Logger.h"

namespace {
    const std::string COLL_CONTAINER = "container_items";
    const std::string COLL_HOTBAR = "hotbar_items";
    const std::string COLL_INVENTORY = "inventory_items";

    constexpr int MAX_ACTIONS_PER_TICK = 8;
    constexpr int MAX_SLOT_ATTEMPTS = 3;
    constexpr int MAX_ORGANIZE_MOVES = 150;
    constexpr int MAX_STABLE_SCANS = 3;

    constexpr int ENCH_PROTECTION = 0;
    constexpr int ENCH_SHARPNESS = 9;

    enum ItemKind : uint8_t {
        KindNone = 0,
        KindArmor,
        KindSword,
        KindBow,
        KindGapple,
        KindEgg,
        KindSnowball,
        KindArrow,
        KindLavaBucket,
        KindFood,
        KindBlock,
        KindJunk,
    };

    struct StackInfo {
        std::string id;
        int count = 0;
        ItemKind kind = KindNone;
        int armorType = -1;
        float gearScore = 0.f;

        bool present() const { return kind != KindNone; }
        bool is(ItemKind k) const { return kind == k; }
    };

    std::string itemIdOf(SDK::ItemStack* stack) {
        auto item = stack->getItem();
        if (!item) return {};
        return item->namespacedId.getString();
    }

    bool endsWith(std::string const& text, std::string const& suffix) {
        if (text.size() < suffix.size()) return false;
        return text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    float durabilityFraction(SDK::ItemStack* stack) {
        auto item = stack->getItem();
        int maxDamage = item ? item->getMaxDamage() : 0;
        if (maxDamage <= 0) return 1.f;
        float used = static_cast<float>(stack->getDamageValue());
        return std::clamp(1.f - used / static_cast<float>(maxDamage), 0.f, 1.f);
    }

    float enchantScore(SDK::ItemStack* stack, int primaryId) {
        auto enchants = stack->gatherEnchants();
        float score = 0.f;
        for (auto const& [id, level] : enchants) {
            score += id == primaryId ? static_cast<float>(level) * 60.f : 8.f * static_cast<float>(level);
        }
        return score;
    }

    int materialRank(std::string const& id) {
        if (id.find("netherite_") != std::string::npos) return 6;
        if (id.find("diamond_") != std::string::npos) return 5;
        if (id.find("iron_") != std::string::npos) return 4;
        if (id.find("chainmail_") != std::string::npos) return 3;
        if (id.find("stone_") != std::string::npos) return 3;
        if (id.find("golden_") != std::string::npos) return 2;
        if (id.find("leather_") != std::string::npos || id.find("wooden_") != std::string::npos) return 1;
        if (id == "minecraft:turtle_helmet") return 4;
        return 0;
    }

    int armorTypeOf(std::string const& id) {
        if (endsWith(id, "_helmet")) return 0;
        if (endsWith(id, "_chestplate")) return 1;
        if (endsWith(id, "_leggings")) return 2;
        if (endsWith(id, "_boots")) return 3;
        return -1;
    }

    int foodRankOf(std::string const& id) {
        if (id == "minecraft:golden_carrot") return 5;
        if (id == "minecraft:cooked_beef" || id == "minecraft:cooked_porkchop") return 4;
        if (id == "minecraft:cooked_mutton" || id == "minecraft:cooked_salmon") return 3;
        return 0;
    }

    bool isBlockId(std::string const& id) {
        if (id == "minecraft:cobblestone" || id == "minecraft:stone") return true;
        return id.find("_planks") != std::string::npos || id.find("wool") != std::string::npos ||
               id.find("concrete") != std::string::npos;
    }

    StackInfo describe(SDK::ItemStack* stack) {
        StackInfo info;
        info.id = itemIdOf(stack);
        if (info.id.empty()) return info;
        info.count = stack->itemCount;

        int armorType = armorTypeOf(info.id);
        if (armorType >= 0) {
            info.kind = KindArmor;
            info.armorType = armorType;
            info.gearScore = static_cast<float>(materialRank(info.id)) * 1000.f +
                             enchantScore(stack, ENCH_PROTECTION) + durabilityFraction(stack) * 30.f;
            return info;
        }

        if (endsWith(info.id, "_sword")) {
            info.kind = KindSword;
            info.gearScore = static_cast<float>(materialRank(info.id)) * 1000.f +
                             enchantScore(stack, ENCH_SHARPNESS) + durabilityFraction(stack) * 30.f;
            return info;
        }

        if (info.id == "minecraft:bow" || info.id == "minecraft:crossbow") info.kind = KindBow;
        else if (info.id == "minecraft:golden_apple" || info.id == "minecraft:appleenchanted") info.kind = KindGapple;
        else if (info.id == "minecraft:egg") info.kind = KindEgg;
        else if (info.id == "minecraft:snowball") info.kind = KindSnowball;
        else if (info.id == "minecraft:arrow") info.kind = KindArrow;
        else if (info.id == "minecraft:lava_bucket") info.kind = KindLavaBucket;
        else if (foodRankOf(info.id) > 0) info.kind = KindFood;
        else if (isBlockId(info.id)) info.kind = KindBlock;
        else info.kind = KindJunk;

        return info;
    }

    StackInfo describeEquipped(SDK::ItemStack* stack) {
        if (!stack || !stack->valid || stack->itemCount == 0 || !stack->getItem()) return {};
        return describe(stack);
    }

    int armorHomeSlot(int armorType) {
        switch (armorType) {
        case 3:
            return 0;
        case 2:
            return 1;
        case 1:
            return 2;
        default:
            return 3;
        }
    }

    int invHomeSlot(StackInfo const& info) {
        switch (info.kind) {
        case KindArmor:
            return armorHomeSlot(info.armorType);
        case KindEgg:
            return 4;
        case KindSnowball:
            return 13;
        case KindFood:
            return 14;
        case KindArrow:
            return 18;
        default:
            return -1;
        }
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
    addSetting("enhanced", LocalizeString::get("client.module.chestStealer.enhanced.name"),
               LocalizeString::get("client.module.chestStealer.enhanced.desc"), enhanced);
    addSetting("autoOrganize", LocalizeString::get("client.module.chestStealer.autoOrganize.name"),
               LocalizeString::get("client.module.chestStealer.autoOrganize.desc"), autoOrganize, "enhanced"_istrue);
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
    stableScans = 0;
    totalMoves = 0;
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
    }
    lastSeen = now;
}

std::chrono::steady_clock::duration ChestStealer::perActionDelay() const {
    float budget = std::get<FloatValue>(time).value;
    if (budget < 0.01f) budget = 0.01f;
    int steps = static_cast<int>(plan.size());
    if (steps < 1) steps = 1;
    return std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(budget / steps));
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
    case Phase::Organize:
        return processOrganize();
    case Phase::Done:
        return false;
    }
    return false;
}

bool ChestStealer::buildPlan(std::chrono::steady_clock::time_point now) {
    if (!containerScreen) {
        if (std::get<BoolValue>(enhanced) && std::get<BoolValue>(autoOrganize)) {
            phase = Phase::Organize;
            return false;
        }
        finish();
        return false;
    }

    bool doEnhanced = std::get<BoolValue>(enhanced);

    float equippedScore[4] = { 0.f, 0.f, 0.f, 0.f };
    float bestOwnedSword = 0.f;

    if (doEnhanced) {
        auto ci = SDK::ClientInstance::get();
        auto lp = ci ? ci->getLocalPlayer() : nullptr;
        if (lp) {
            for (int i = 0; i < 4; i++) {
                auto info = describeEquipped(lp->getArmor(i));
                if (info.is(KindArmor) && info.armorType == i) equippedScore[i] = info.gearScore;
            }
        }
        for (int i = 0; i < 9; i++) {
            auto stack = readSlot(COLL_HOTBAR, i);
            if (!stack) continue;
            auto info = describe(stack);
            if (info.is(KindSword)) bestOwnedSword = std::max(bestOwnedSword, info.gearScore);
        }
        for (int i = 0; i < 27; i++) {
            auto stack = readSlot(COLL_INVENTORY, i);
            if (!stack) continue;
            auto info = describe(stack);
            if (info.is(KindSword)) bestOwnedSword = std::max(bestOwnedSword, info.gearScore);
        }
    }

    float bestChestArmor[4] = { -1.f, -1.f, -1.f, -1.f };
    int bestChestArmorSlot[4] = { -1, -1, -1, -1 };
    float bestChestSword = -1.f;
    int bestChestSwordSlot = -1;

    int found = 0;
    std::string dump;
    for (int i = 0; i < 54; i++) {
        auto stack = readSlot(COLL_CONTAINER, i);
        if (!stack) continue;
        found++;

        if (doEnhanced) {
            auto info = describe(stack);
            if (info.is(KindArmor)) {
                if (info.gearScore > equippedScore[info.armorType] && info.gearScore > bestChestArmor[info.armorType]) {
                    bestChestArmor[info.armorType] = info.gearScore;
                    bestChestArmorSlot[info.armorType] = i;
                }
                continue;
            }
            if (info.is(KindSword)) {
                if (info.gearScore > bestOwnedSword && info.gearScore > bestChestSword) {
                    bestChestSword = info.gearScore;
                    bestChestSwordSlot = i;
                }
                continue;
            }
        }

        if (dump.size() < 160) {
            if (!dump.empty()) dump += ", ";
            dump += std::to_string(i) + ":" + itemIdOf(stack) + "x" + std::to_string(stack->itemCount);
        }
        plan.push_back(i);
    }

    if (doEnhanced) {
        for (int t = 0; t < 4; t++) {
            if (bestChestArmorSlot[t] >= 0) plan.push_back(bestChestArmorSlot[t]);
        }
        if (bestChestSwordSlot >= 0) plan.push_back(bestChestSwordSlot);
    }

    if (found == 0 && now - sessionStart < 1500ms) {
        plan.clear();
        return false;
    }

    lastAction = now;
    if (plan.empty()) {
        if (doEnhanced) {
            phase = Phase::Organize;
        } else {
            finish();
        }
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

    if (std::get<BoolValue>(enhanced)) {
        phase = Phase::Organize;
    } else {
        finish();
    }
    return false;
}

bool ChestStealer::tryMove(const std::string& srcColl, int srcIdx, const std::string& dstColl, int dstIdx) {
    if (srcColl == dstColl && srcIdx == dstIdx) return false;
    int moved = controller->transferSlot(srcColl, srcIdx, dstColl, dstIdx);
    if (moved <= 0) return false;
    totalMoves++;
    return true;
}

bool ChestStealer::processOrganize() {
    bool full = std::get<BoolValue>(autoOrganize).value;
    if (totalMoves >= MAX_ORGANIZE_MOVES) {
        finish();
        return false;
    }

    StackInfo hotbar[9];
    StackInfo inv[27];
    bool hotbarPinned[9] = {};
    for (int i = 0; i < 9; i++) {
        auto stack = readSlot(COLL_HOTBAR, i);
        hotbar[i] = stack ? describe(stack) : StackInfo {};
    }
    for (int i = 0; i < 27; i++) {
        auto stack = readSlot(COLL_INVENTORY, i);
        inv[i] = stack ? describe(stack) : StackInfo {};
    }
    for (int i = 0; i < 9; i++) {
        if (!hotbar[i].present()) continue;
        bool belongs = hotbar[i].is(KindSword) || hotbar[i].is(KindBow) || hotbar[i].is(KindGapple) ||
                       hotbar[i].is(KindFood) || hotbar[i].is(KindBlock) || hotbar[i].is(KindLavaBucket);
        if (!belongs) hotbarPinned[i] = true;
    }

    auto slotCategory = [](int idx) -> ItemKind {
        if (idx <= 3) return KindArmor;
        if (idx == 4) return KindEgg;
        if (idx <= 8) return KindBlock;
        if (idx == 13) return KindSnowball;
        if (idx >= 14 && idx <= 17) return KindFood;
        if (idx == 18 || idx == 19) return KindArrow;
        return KindNone;
    };

    auto flexInvSlot = [&]() -> int {
        for (int i = 20; i <= 26; i++) {
            if (!inv[i].present()) return i;
        }
        return -1;
    };

    auto tryMoveInv = [&](int srcIdx, int dstIdx) -> bool {
        return tryMove(COLL_INVENTORY, srcIdx, COLL_INVENTORY, dstIdx);
    };

    if (full) {
        for (int i = 0; i <= 18; i++) {
            if (!inv[i].present()) continue;
            ItemKind want = slotCategory(i);
            if (want == KindNone) continue;
            if (inv[i].is(want)) continue;
            if (want == KindArmor && inv[i].is(KindArmor)) continue;
            int flex = flexInvSlot();
            if (flex < 0) break;
            if (tryMoveInv(i, flex)) return true;
        }
    }

    bool slotTaken[9] = {};

    auto assignHotbar = [&](ItemKind kind, int defaultSlot) -> int {
        for (int i = 0; i < 9; i++) {
            if (hotbar[i].is(kind) && !slotTaken[i]) {
                slotTaken[i] = true;
                return i;
            }
        }
        if (!hotbar[defaultSlot].present() && !slotTaken[defaultSlot] && !hotbarPinned[defaultSlot]) {
            slotTaken[defaultSlot] = true;
            return defaultSlot;
        }
        for (int i = 0; i < 9; i++) {
            if (slotTaken[i] || hotbarPinned[i]) continue;
            if (!full && hotbar[i].present()) continue;
            if (hotbar[i].present() && !hotbar[i].is(kind)) continue;
            slotTaken[i] = true;
            return i;
        }
        return -1;
    };

    int swordSlot = assignHotbar(KindSword, 0);
    int gappleSlot = assignHotbar(KindGapple, 1);
    int bowSlot = assignHotbar(KindBow, 2);

    int blockPrimary = -1;
    int blockSecondary = -1;
    int bestBlockCount = -1;
    for (int i = 0; i < 9; i++) {
        if (slotTaken[i] || hotbarPinned[i]) continue;
        if (!hotbar[i].is(KindBlock)) continue;
        slotTaken[i] = true;
        if (hotbar[i].count > bestBlockCount) {
            bestBlockCount = hotbar[i].count;
            blockSecondary = blockPrimary;
            blockPrimary = i;
        } else if (blockSecondary < 0) {
            blockSecondary = i;
        }
    }
    if (blockPrimary < 0) blockPrimary = assignHotbar(KindBlock, 3);
    if (blockSecondary < 0) blockSecondary = assignHotbar(KindBlock, 5);

    int foodSlot = assignHotbar(KindFood, 4);
    int lavaSlot = assignHotbar(KindLavaBucket, 6);

    if (full && blockPrimary >= 0 && hotbar[blockPrimary].is(KindBlock) && hotbar[blockPrimary].count < 64) {
        for (int i = 5; i <= 8; i++) {
            if (!inv[i].present() || inv[i].id != hotbar[blockPrimary].id) continue;
            if (tryMove(COLL_INVENTORY, i, COLL_HOTBAR, blockPrimary)) return true;
        }
        for (int i = 0; i < 54; i++) {
            auto stack = readSlot(COLL_CONTAINER, i);
            if (!stack) continue;
            if (itemIdOf(stack) != hotbar[blockPrimary].id) continue;
            if (tryMove(COLL_CONTAINER, i, COLL_HOTBAR, blockPrimary)) return true;
        }
    }

    auto pushToHome = [&](StackInfo const& info, int srcIdx) -> bool {
        int home = invHomeSlot(info);
        if (home < 0) return false;
        if (srcIdx == home) return false;
        if (!inv[home].present()) return tryMoveInv(srcIdx, home);
        if (inv[home].id == info.id && inv[home].count < 64) return tryMoveInv(srcIdx, home);
        return false;
    };

    for (int i = 0; i < 27; i++) {
        if (!inv[i].present()) continue;
        if (pushToHome(inv[i], i)) return true;
    }

    for (int t = 0; t < 4; t++) {
        int home = armorHomeSlot(t);
        if (!inv[home].is(KindArmor) || inv[home].armorType != t) continue;
        StackInfo current = inv[home];
        int betterIdx = -1;
        for (int i = 0; i < 27; i++) {
            if (i == home || !inv[i].is(KindArmor) || inv[i].armorType != t) continue;
            if (inv[i].gearScore > current.gearScore) {
                betterIdx = i;
                break;
            }
        }
        if (betterIdx < 0) continue;
        int flex = flexInvSlot();
        if (flex < 0) continue;
        if (tryMoveInv(home, flex)) return true;
    }

    for (int i = 0; i < 27; i++) {
        if (!inv[i].is(KindFood)) continue;
        if (i >= 14 && i <= 17) continue;
        for (int s = 14; s <= 17; s++) {
            if (inv[s].present()) continue;
            if (tryMoveInv(i, s)) return true;
        }
        for (int s = 14; s <= 17; s++) {
            if (inv[s].id == inv[i].id && inv[s].count < 64) {
                if (tryMoveInv(i, s)) return true;
            }
        }
        break;
    }

    for (int i = 0; i < 27; i++) {
        if (!inv[i].is(KindBlock)) continue;
        if (i >= 5 && i <= 8) continue;
        for (int s = 5; s <= 8; s++) {
            if (inv[s].present()) continue;
            if (tryMoveInv(i, s)) return true;
        }
        for (int s = 5; s <= 8; s++) {
            if (inv[s].id == inv[i].id && inv[s].count < 64) {
                if (tryMoveInv(i, s)) return true;
            }
        }
        break;
    }

    if (full) {
        struct Promote {
            ItemKind kind;
            int slot;
        };
        Promote promotes[] = {
            { KindSword, swordSlot },   { KindGapple, gappleSlot }, { KindBow, bowSlot },
            { KindBlock, blockPrimary }, { KindFood, foodSlot },     { KindLavaBucket, lavaSlot },
        };
        for (auto const& promote : promotes) {
            if (promote.slot < 0) continue;
            for (int i = 0; i < 27; i++) {
                if (!inv[i].is(promote.kind)) continue;
                if (!hotbar[promote.slot].present()) {
                    if (tryMove(COLL_INVENTORY, i, COLL_HOTBAR, promote.slot)) return true;
                } else if (hotbar[promote.slot].id == inv[i].id && hotbar[promote.slot].count < 64) {
                    if (tryMove(COLL_INVENTORY, i, COLL_HOTBAR, promote.slot)) return true;
                }
                break;
            }
        }
    }

    if (full) {
        for (int i = 9; i <= 12; i++) {
            if (inv[i].present()) continue;
            for (int s = 20; s <= 26; s++) {
                if (!inv[s].is(KindArmor)) continue;
                if (tryMoveInv(s, i)) return true;
            }
        }
    }

    stableScans++;
    if (stableScans >= MAX_STABLE_SCANS) {
        finish();
    }
    return false;
}

void ChestStealer::finish() {
    phase = Phase::Done;
    if (!std::get<BoolValue>(closeAfterLoot) || exitRequested) return;
    if (!containerScreen || !controller || lootedStacks <= 0) return;
    exitRequested = true;
    controller->tryExit();
}
