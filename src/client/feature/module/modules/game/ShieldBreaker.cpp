#include "pch.h"
#include "ShieldBreaker.h"
#include "client/misc/EntityCache.h"
#include "client/misc/PlayerListManager.h"
#include "client/misc/TargetManager.h"
#include "client/misc/WallCheck.h"
#include <client/screen/ScreenManager.h>
#include <client/event/events/AfterMoveEvent.h>
#include <mc/Addresses.h>
#include <mc/common/client/game/ClientInstance.h>
#include <mc/common/client/game/MinecraftGame.h>
#include <mc/common/client/player/LocalPlayer.h>
#include <mc/common/entity/component/ActorEquipmentComponent.h>
#include <mc/common/world/Minecraft.h>
#include <mc/common/world/level/Level.h>
#include <mc/common/world/level/HitResult.h>
#include <mc/common/world/level/BlockSource.h>
#include <mc/common/world/level/Dimension.h>
#include <mc/common/world/actor/Actor.h>
#include <mc/common/world/actor/player/Player.h>
#include <mc/common/world/actor/player/PlayerInventory.h>
#include <mc/common/world/actor/player/Inventory.h>
#include <mc/common/world/actor/player/GameMode.h>
#include <mc/common/world/ItemStack.h>

namespace {
    constexpr int actorFlagBlocking = 72;
    constexpr int handSlotMain = 0;
    constexpr int handSlotOff = 1;
    constexpr int hotbarSlots = 9;
    constexpr float defaultReach = 3.f;

    bool isShield(SDK::ItemStack* stack) {
        if (!stack) return false;
        auto* item = stack->getItem();
        if (!item) return false;
        return item->namespacedId.getString() == "minecraft:shield";
    }

    bool isAxe(SDK::ItemStack* stack) {
        if (!stack) return false;
        auto* item = stack->getItem();
        if (!item) return false;
        std::string id = item->namespacedId.getString();
        return id.ends_with("_axe");
    }

    Vec3 rayDirectionFromHit(SDK::HitResult* hit) {
        Vec3 toHit = hit->hitPos - hit->start;
        if (toHit.magnitude() > 0.001f) return toHit.normalized();
        return hit->end.magnitude() > 0.0001f ? hit->end.normalized() : Vec3 { 0.f, 0.f, 0.f };
    }
}

ShieldBreaker::ShieldBreaker()
    : Module("ShieldBreaker", LocalizeString::get("client.module.shieldbreaker.name"),
             LocalizeString::get("client.module.shieldbreaker.desc"), GAME, nokeybind) {
    addSetting("useDefaultRange", LocalizeString::get("client.module.shieldbreaker.useDefaultRange.name"),
               LocalizeString::get("client.module.shieldbreaker.useDefaultRange.desc"), useDefaultRange);

    addSliderSetting("range", LocalizeString::get("client.module.shieldbreaker.range.name"),
                     LocalizeString::get("client.module.shieldbreaker.range.desc"), range, FloatValue(1.f),
                     FloatValue(20.f), FloatValue(0.5f), "useDefaultRange"_isfalse);

    addSetting("requireBlocking", LocalizeString::get("client.module.shieldbreaker.requireBlocking.name"),
               LocalizeString::get("client.module.shieldbreaker.requireBlocking.desc"), requireBlocking);
    addSetting("wallCheck", LocalizeString::get("client.module.shieldbreaker.wallCheck.name"),
               LocalizeString::get("client.module.shieldbreaker.wallCheck.desc"), wallCheck);
    addSetting("ignoreFriends", LocalizeString::get("client.module.shieldbreaker.ignoreFriends.name"),
               LocalizeString::get("client.module.shieldbreaker.ignoreFriends.desc"), ignoreFriends);

    addSliderSetting("switchDelay", LocalizeString::get("client.module.shieldbreaker.switchDelay.name"),
                     LocalizeString::get("client.module.shieldbreaker.switchDelay.desc"), switchDelay, FloatValue(0.f),
                     FloatValue(50.f), FloatValue(1.f));

    addSliderSetting("cooldown", LocalizeString::get("client.module.shieldbreaker.cooldown.name"),
                     LocalizeString::get("client.module.shieldbreaker.cooldown.desc"), cooldown, FloatValue(0.f),
                     FloatValue(5.f), FloatValue(0.1f));

    this->listen<UpdateEvent>(&ShieldBreaker::onUpdate);
    this->listen<AfterMoveEvent>(&ShieldBreaker::onAfterMove);
}

void ShieldBreaker::onDisable() {
    auto ci = SDK::ClientInstance::get();
    finishPendingSwap(ci ? ci->getLocalPlayer() : nullptr);
    nextAttack = std::chrono::steady_clock::now();
    lastBrokenTarget = 0;
    lastBreakTime = {};
}

void ShieldBreaker::clearPendingSwap() {
    phase = Phase::Idle;
    savedSlot = -1;
    pendingAxeSlot = -1;
    pendingTargetId = 0;
    switchedAt = {};
}

void ShieldBreaker::finishPendingSwap(SDK::Player* lp) {
    if (lp && lp->supplies && savedSlot >= 0) lp->supplies->selectedSlot = savedSlot;
    clearPendingSwap();
}

bool ShieldBreaker::performAttack(SDK::LocalPlayer* lp, SDK::Actor* target) {
    if (!lp->gameMode || !Signatures::GameMode_attack.result) return false;

    Vec3 clickPos = target->getBoundingBox().getCenter();

    using GameModeAttackFn = __int64 (*)(void*, SDK::Actor*, char, Vec3*);
    reinterpret_cast<GameModeAttackFn>(Signatures::GameMode_attack.result)(lp->gameMode, target, 0, &clickPos);
    return true;
}

void ShieldBreaker::onAfterMove(Event&) {
    if (phase != Phase::ReadyToHit) return;

    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (!lp || !lp->supplies) {
        clearPendingSwap();
        return;
    }

    SDK::Actor* pending = EntityCache::get().findByRuntimeID(pendingTargetId);
    if (pending && performAttack(lp, pending)) {
        TargetManager::setTarget(pending);
        auto now = std::chrono::steady_clock::now();
        lastBrokenTarget = pendingTargetId;
        lastBreakTime = now;
        nextAttack = now + std::chrono::milliseconds(100);
    }
    finishPendingSwap(lp);
}

bool ShieldBreaker::targetHasShield(SDK::Actor* target) {
    auto* equip = target->tryGetComponent<SDK::ActorEquipmentComponent>();
    if (!equip || !equip->handContainer) return false;
    return isShield(equip->handContainer->getItem(handSlotMain)) ||
           isShield(equip->handContainer->getItem(handSlotOff));
}

bool ShieldBreaker::targetIsBlocking(SDK::Actor* target) {
    if (!std::get<BoolValue>(requireBlocking)) return true;
    return target->getStatusFlag(actorFlagBlocking);
}

int ShieldBreaker::findAxeSlot(SDK::Player* lp) {
    if (!lp->supplies || !lp->supplies->inventory) return -1;
    for (int slot = 0; slot < hotbarSlots; ++slot) {
        if (isAxe(lp->supplies->inventory->getItem(slot))) return slot;
    }
    return -1;
}

SDK::Actor* ShieldBreaker::pickTarget(SDK::LocalPlayer* lp, float maxRange) {
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return nullptr;

    auto level = ci->minecraft->getLevel();
    if (!level) return nullptr;

    auto hit = level->getHitResult();
    if (!hit) return nullptr;

    Vec3 direction = rayDirectionFromHit(hit);
    if (direction.magnitude() <= 0.0001f) return nullptr;

    float nearest = maxRange;
    if (hit->hitType == SDK::HitType::BLOCK) {
        float blockDist = hit->start.distance(hit->hitPos);
        if (blockDist > 0.001f) nearest = std::min(nearest, blockDist + 0.05f);
    }

    bool doIgnoreFriends = std::get<BoolValue>(ignoreFriends);
    bool doWallCheck = std::get<BoolValue>(wallCheck);
    SDK::BlockSource* region = doWallCheck ? ci->getRegion() : nullptr;
    if (region) WallCheck::beginPass();

    SDK::Actor* best = nullptr;
    auto snap = EntityCache::get().snapshot();
    for (auto const& view : snap->views) {
        SDK::Actor* entt = view.actor;
        if (!entt || entt == lp || !entt->aabbShape) continue;
        if (!view.isPlayer || view.invisible) continue;
        if (!view.hasHealth || view.health <= 0.f) continue;
        if (doIgnoreFriends && PlayerListManager::get().isFriend(reinterpret_cast<SDK::Player*>(entt)->playerName))
            continue;

        auto hitDist = entt->getBoundingBox().intersectsRay(hit->start, direction, nearest, 0.08f);
        if (!hitDist || *hitDist >= nearest) continue;

        if (region) {
            Vec3 impact { hit->start.x + direction.x * *hitDist, hit->start.y + direction.y * *hitDist,
                          hit->start.z + direction.z * *hitDist };
            if (!WallCheck::isVisible(region, hit->start, impact)) continue;
        }

        nearest = *hitDist;
        best = entt;
    }
    return best;
}

void ShieldBreaker::onUpdate(Event&) {
    auto ci = SDK::ClientInstance::get();
    auto lpEarly = ci ? ci->getLocalPlayer() : nullptr;

    if (!ci || !ci->minecraft || !ci->minecraftGame || !ci->minecraftGame->isCursorGrabbed() ||
        Necromancer::get().getScreenManager().getActiveScreen()) {
        finishPendingSwap(lpEarly);
        return;
    }

    auto lp = lpEarly;
    if (!lp || !lp->gameMode || !lp->supplies || !lp->supplies->inventory) {
        clearPendingSwap();
        return;
    }

    auto now = std::chrono::steady_clock::now();

    if (phase == Phase::HoldingAxe) {
        float delayMs = std::clamp(std::get<FloatValue>(switchDelay).value, 0.f, 50.f);
        auto elapsed = std::chrono::duration<float, std::milli>(now - switchedAt).count();

        if (pendingAxeSlot >= 0) lp->supplies->selectedSlot = pendingAxeSlot;

        if (elapsed < delayMs) return;

        phase = Phase::ReadyToHit;
        return;
    }

    if (phase == Phase::ReadyToHit) {
        if (std::chrono::duration<float, std::milli>(now - switchedAt).count() > 500.f) {
            finishPendingSwap(lp);
            return;
        }
        if (pendingAxeSlot >= 0) lp->supplies->selectedSlot = pendingAxeSlot;
        return;
    }

    int axeSlot = findAxeSlot(lp);
    if (axeSlot < 0) return;

    float maxRange = std::get<BoolValue>(useDefaultRange) ? defaultReach : std::get<FloatValue>(range).value;

    auto* target = pickTarget(lp, maxRange);
    if (!target) return;
    if (!targetIsBlocking(target)) return;
    if (!targetHasShield(target)) return;

    uint64_t targetId = target->getRuntimeID();
    float cooldownSec = std::clamp(std::get<FloatValue>(cooldown).value, 0.f, 5.f);
    if (cooldownSec > 0.f && lastBrokenTarget == targetId &&
        lastBreakTime != std::chrono::steady_clock::time_point {}) {
        auto elapsed = std::chrono::duration<float>(now - lastBreakTime).count();
        if (elapsed < cooldownSec) return;
    }

    if (now < nextAttack) return;

    int originalSlot = lp->supplies->selectedSlot;
    if (originalSlot == axeSlot) {
        phase = Phase::ReadyToHit;
        pendingAxeSlot = axeSlot;
        savedSlot = originalSlot;
        pendingTargetId = targetId;
        switchedAt = now;
        return;
    }

    lp->supplies->selectedSlot = axeSlot;
    phase = Phase::HoldingAxe;
    savedSlot = originalSlot;
    pendingAxeSlot = axeSlot;
    pendingTargetId = targetId;
    switchedAt = now;
}
