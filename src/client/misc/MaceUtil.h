#pragma once
#include "util/LMath.h"
#include <mc/common/world/actor/Actor.h>
#include <mc/common/world/actor/player/Player.h>
#include <mc/common/world/actor/player/PlayerInventory.h>
#include <mc/common/world/actor/player/Inventory.h>
#include <mc/common/world/ItemStack.h>
#include <mc/common/entity/component/FallDistanceComponent.h>

namespace MaceUtil {
    constexpr float smashMinFall = 1.5f;

    inline float getFallDistance(SDK::Actor* actor) {
        if (!actor) return 0.f;
        auto* comp = actor->tryGetComponent<SDK::FallDistanceComponent>();
        return comp ? comp->fallDistance : 0.f;
    }

    inline bool isMace(SDK::ItemStack* stack) {
        if (!stack || !stack->getItem()) return false;
        return stack->getItem()->namespacedId.getString() == "minecraft:mace";
    }

    inline bool isHoldingMace(SDK::Player* lp) {
        if (!lp || !lp->supplies || !lp->supplies->inventory) return false;
        return isMace(lp->supplies->inventory->getItem(lp->supplies->selectedSlot));
    }

    inline int findMaceSlot(SDK::Player* lp) {
        if (!lp || !lp->supplies || !lp->supplies->inventory) return -1;
        for (int i = 0; i < 9; i++) {
            if (isMace(lp->supplies->inventory->getItem(i))) return i;
        }
        return -1;
    }

    inline bool canSmash(SDK::Actor* actor) {
        return getFallDistance(actor) >= smashMinFall;
    }

    inline float smashBonusDamage(float fallDistance) {
        if (fallDistance < smashMinFall) return 0.f;
        float bonus = 0.f;
        float remaining = fallDistance;
        float first = std::min(remaining, 3.f);
        bonus += first * 4.f;
        remaining -= first;
        if (remaining > 0.f) {
            float second = std::min(remaining, 5.f);
            bonus += second * 2.f;
            remaining -= second;
        }
        if (remaining > 0.f) bonus += remaining * 1.f;
        return bonus;
    }
}
