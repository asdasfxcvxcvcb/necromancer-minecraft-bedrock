#include "pch.h"
#include "NoFall.h"
#include <client/screen/ScreenManager.h>
#include <client/misc/MovementSim.h>
#include <client/misc/MaceUtil.h>
#include <mc/common/client/game/ClientInstance.h>
#include <mc/common/client/game/MinecraftGame.h>
#include <mc/common/client/game/MouseDevice.h>
#include <mc/common/client/game/MouseAction.h>
#include <mc/common/client/player/LocalPlayer.h>
#include <mc/common/world/Minecraft.h>
#include <mc/common/world/level/Level.h>
#include <mc/common/world/level/HitResult.h>
#include <mc/common/world/actor/player/Player.h>
#include <mc/common/world/actor/player/PlayerInventory.h>
#include <mc/common/world/actor/player/Inventory.h>
#include <mc/common/world/ItemStack.h>
#include <mc/common/util/BasicPrintStream.h>
#include <mc/common/nbt/CompoundTag.h>
#include <mc/common/network/Packet.h>
#include <client/event/events/SendPacketEvent.h>
#include <client/event/events/TickEvent.h>
#include <client/event/events/AfterMoveEvent.h>

namespace {
    constexpr int enchIdProtection = 0;
    constexpr int enchIdFeatherFalling = 2;
    constexpr int actorFlagGliding = 32;
    constexpr float eyeDropAllowance = 1.0f;
    constexpr float safeFallBlocks = 3.f;
    constexpr float gravityPerTick = 0.08f;
    constexpr float verticalDragPerTick = 0.98f;
    constexpr float terminalVelocityY = -3.92f;

    float advanceVelY(float velY) {
        float v = (velY - gravityPerTick) * verticalDragPerTick;
        return v < terminalVelocityY ? terminalVelocityY : v;
    }

    float wrapAngle(float angle) {
        while (angle > 180.f) angle -= 360.f;
        while (angle < -180.f) angle += 360.f;
        return angle;
    }

    void pushAction(int button, bool down) {
        auto mouse = SDK::MouseDevice::get();
        if (!mouse) return;

        SDK::MouseAction action {};
        action.x = mouse->x;
        action.y = mouse->y;
        action.dx = 0;
        action.dy = 0;
        action.action = static_cast<int8_t>(button);
        action.data = down ? int8_t { 1 } : int8_t { 0 };
        action.pointerId = 0;
        action.forceMotionlessPointer = false;
        mouse->inputs.push_back(action);
    }

    int findWaterBucketSlot(SDK::Player* lp) {
        if (!lp->supplies || !lp->supplies->inventory) return -1;
        for (int i = 0; i < 9; i++) {
            auto* stack = lp->supplies->inventory->getItem(i);
            if (!stack || !stack->getItem()) continue;
            if (stack->getItem()->namespacedId.getString() == "minecraft:water_bucket") return i;
        }
        return -1;
    }

    int findEmptyBucketSlot(SDK::Player* lp) {
        if (!lp->supplies || !lp->supplies->inventory) return -1;
        for (int i = 0; i < 9; i++) {
            auto* stack = lp->supplies->inventory->getItem(i);
            if (!stack || !stack->getItem()) continue;
            if (stack->getItem()->namespacedId.getString() == "minecraft:bucket") return i;
        }
        return -1;
    }

    bool isHoldingMace(SDK::Player* lp) {
        return MaceUtil::isHoldingMace(lp);
    }

    int getEnchantLevel(SDK::ItemStack* stack, int enchId) {
        if (!stack || !stack->tag) return 0;

        SDK::BasicPrintStream ps {};
        stack->tag->print("", ps);
        std::string const& dump = ps.mStr;

        auto enchPos = dump.find("\"ench\"");
        if (enchPos == std::string::npos) return 0;

        auto parseNum = [&](size_t colonPos) {
            size_t i = colonPos + 1;
            while (i < dump.size() && (dump[i] == ' ' || dump[i] == '\t')) i++;
            bool neg = false;
            if (i < dump.size() && dump[i] == '-') {
                neg = true;
                i++;
            }
            int val = 0;
            bool any = false;
            while (i < dump.size() && dump[i] >= '0' && dump[i] <= '9') {
                val = val * 10 + (dump[i] - '0');
                i++;
                any = true;
            }
            return any ? (neg ? -val : val) : 0;
        };

        size_t searchFrom = enchPos;
        while (true) {
            auto idPos = dump.find("\"id\"", searchFrom);
            if (idPos == std::string::npos) return 0;
            auto idColon = dump.find(':', idPos);
            if (idColon == std::string::npos) return 0;
            int id = parseNum(idColon);

            auto lvlPos = dump.find("\"lvl\"", idColon);
            if (lvlPos == std::string::npos) return 0;
            auto lvlColon = dump.find(':', lvlPos);
            if (lvlColon == std::string::npos) return 0;
            int lvl = parseNum(lvlColon);

            if (id == enchId) return lvl;
            searchFrom = lvlColon;
        }
    }

    float fallProtectionFactor(SDK::Player* lp) {
        int epf = 0;
        for (int slot = 0; slot < 4; slot++) {
            auto* armor = lp->getArmor(slot);
            if (!armor || !armor->getItem()) continue;
            epf += getEnchantLevel(armor, enchIdProtection);
            epf += getEnchantLevel(armor, enchIdFeatherFalling) * 3;
        }
        epf = std::min(20, epf);
        return 1.f - static_cast<float>(epf) * 0.04f;
    }
}

NoFall::NoFall()
    : Module("NoFall", LocalizeString::get("client.module.nofall.name"),
             LocalizeString::get("client.module.nofall.desc"), GAME, nokeybind) {
    mode.addEntry(EnumEntry(0, LocalizeString::get("client.module.nofall.mode.autoWater.name"),
                            LocalizeString::get("client.module.nofall.mode.autoWater.desc")));
    addEnumSetting("mode", LocalizeString::get("client.module.nofall.mode.name"),
                   LocalizeString::get("client.module.nofall.mode.desc"), mode);

    Setting::Condition autoWaterCond(std::vector<Setting::SingleCond> {
        { "mode", { 0 }, false },
    });
    addSliderSetting("minDamage", LocalizeString::get("client.module.nofall.minDamage.name"),
                     LocalizeString::get("client.module.nofall.minDamage.desc"), minDamage, FloatValue(0.f),
                     FloatValue(20.f), FloatValue(1.f), autoWaterCond);
    addSliderSetting("aimDistance", LocalizeString::get("client.module.nofall.aimDistance.name"),
                     LocalizeString::get("client.module.nofall.aimDistance.desc"), aimDistance, FloatValue(5.f),
                     FloatValue(100.f), FloatValue(5.f), autoWaterCond);
    addSliderSetting("preSwitchDistance", LocalizeString::get("client.module.nofall.preSwitchDistance.name"),
                     LocalizeString::get("client.module.nofall.preSwitchDistance.desc"), preSwitchDistance,
                     FloatValue(3.f), FloatValue(60.f), FloatValue(1.f), autoWaterCond);
    addSliderSetting("placeReach", LocalizeString::get("client.module.nofall.placeReach.name"),
                     LocalizeString::get("client.module.nofall.placeReach.desc"), placeReach, FloatValue(2.5f),
                     FloatValue(7.f), FloatValue(0.1f), autoWaterCond);
    addSliderSetting("armTickLead", LocalizeString::get("client.module.nofall.armTickLead.name"),
                     LocalizeString::get("client.module.nofall.armTickLead.desc"), armTickLead, FloatValue(4.f),
                     FloatValue(40.f), FloatValue(1.f), autoWaterCond);
    addSetting("disableWithMace", LocalizeString::get("client.module.nofall.disableWithMace.name"),
               LocalizeString::get("client.module.nofall.disableWithMace.desc"), disableWithMace, autoWaterCond);
    addSetting("forceWhenLow", LocalizeString::get("client.module.nofall.forceWhenLow.name"),
               LocalizeString::get("client.module.nofall.forceWhenLow.desc"), forceWhenLow, autoWaterCond);
    addSetting("ignorePlacedWater", LocalizeString::get("client.module.nofall.ignorePlacedWater.name"),
               LocalizeString::get("client.module.nofall.ignorePlacedWater.desc"), ignorePlacedWater, autoWaterCond);
    addSetting("pickUpWater", LocalizeString::get("client.module.nofall.pickUpWater.name"),
               LocalizeString::get("client.module.nofall.pickUpWater.desc"), pickUpWater, autoWaterCond);

    addSetting("useFakelag", LocalizeString::get("client.module.nofall.useFakelag.name"),
               LocalizeString::get("client.module.nofall.useFakelag.desc"), useFakelag, autoWaterCond);
    Setting::Condition fakelagCond(std::vector<Setting::SingleCond> {
        { "mode", { 0 }, false },
        { "useFakelag", { 1 }, false },
    });
    addSliderSetting("chokeTicks", LocalizeString::get("client.module.nofall.chokeTicks.name"),
                     LocalizeString::get("client.module.nofall.chokeTicks.desc"), chokeTicks, FloatValue(1.f),
                     FloatValue(20.f), FloatValue(1.f), fakelagCond);
    addSetting("freeEyeWhileFrozen", LocalizeString::get("client.module.nofall.freeEyeWhileFrozen.name"),
               LocalizeString::get("client.module.nofall.freeEyeWhileFrozen.desc"), freeEyeWhileFrozen, fakelagCond);

    this->listen<UpdateEvent>(&NoFall::onUpdate);
    this->listen<TickEvent>((EventListenerFunc)&NoFall::onTick);
    this->listen<SendPacketEvent>((EventListenerFunc)&NoFall::onSendPacket);
    this->listen<AfterMoveEvent>((EventListenerFunc)&NoFall::onAfterMove);
}

void NoFall::startChoke() {
    if (chokeActive) return;
    chokeActive = true;
    chokedPackets = 0;
    allowOnePacket = false;

    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (lp && lp->aabbShape) {
        freezePos = lp->getPos();
        freezeActive = true;
    }
}

void NoFall::stopChoke() {
    if (!chokeActive && !freezeActive) return;
    if (freezeActive) unfrozeAt = std::chrono::steady_clock::now();
    chokeActive = false;
    freezeActive = false;
    chokedPackets = 0;
    allowOnePacket = false;
}

void NoFall::onAfterMove(Event&) {
    if (!freezeActive) return;

    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (!lp || !lp->aabbShape || !lp->stateVector) {
        freezeActive = false;
        return;
    }

    Vec3& pos = lp->getPos();

    float drift = freezePos.y - pos.y;
    float allowedDrop = std::get<BoolValue>(freeEyeWhileFrozen) ? eyeDropAllowance : 0.f;
    float eyeY = freezePos.y - std::clamp(drift, 0.f, allowedDrop);

    Vec3 pinned { freezePos.x, eyeY, freezePos.z };
    Vec3 boxDelta { freezePos.x - pos.x, freezePos.y - pos.y, freezePos.z - pos.z };

    pos = pinned;
    lp->getPosOld() = pinned;
    lp->getVelocity() = Vec3 { 0.f, 0.f, 0.f };

    AABB& box = lp->getBoundingBox();
    box.lower = box.lower + boxDelta;
    box.higher = box.higher + boxDelta;
}

void NoFall::onSendPacket(Event& evG) {
    if (!chokeActive) return;

    auto& ev = reinterpret_cast<SendPacketEvent&>(evG);
    auto* packet = ev.getPacket();
    if (!packet) return;

    auto id = packet->getID();
    if (id != SDK::PacketID::PLAYER_AUTH_INPUT && id != SDK::PacketID::MOVE_PLAYER) return;

    if (allowOnePacket) {
        allowOnePacket = false;
        return;
    }

    chokedPackets++;
    ev.setCancelled(true);
}

void NoFall::afterLoadConfig() {
    if (mode.getSelectedKey() != 0) mode.setSelectedKey(0);
}

void NoFall::onEnable() {
    fallDistance = 0.f;
    state = ClutchState::Idle;
    originalSlot = -1;
    clutchBucketSlot = -1;
    hasLastY = false;
    cooldownUntil = {};
    placedAt = {};
    lastAimFrame = {};
    protectionCachedAt = {};
    lastWindowLog = {};
    slowFallSince = {};
    lastBucketWarn = {};
    landedAt = {};
    pickupStartedAt = {};
    pickupClickedAt = {};
    preSwitched = false;
    placeAttempts = 0;
}

void NoFall::onDisable() {
    abortClutch(SDK::ClientInstance::get() ? SDK::ClientInstance::get()->getLocalPlayer() : nullptr, "disabled");
    fallDistance = 0.f;
    hasLastY = false;
    cooldownUntil = {};
}

void NoFall::restoreSlot(SDK::Player* lp) {
    if (originalSlot < 0) return;
    if (lp && lp->supplies && lp->supplies->selectedSlot != originalSlot) {
        lp->supplies->selectedSlot = originalSlot;
    }
    originalSlot = -1;
}

void NoFall::resetClutch() {
    stopChoke();
    state = ClutchState::Idle;
    lastAimFrame = {};
    clutchBucketSlot = -1;
    placeAttempts = 0;
    preSwitched = false;
    landedAt = {};
    pickupStartedAt = {};
    pickupClickedAt = {};
    lastStateLog = {};
    chokeStartedAt = {};
    sawFallingAfterPlace = false;
    unfrozeAt = {};
}

void NoFall::abortClutch(SDK::Player* lp, char const* reason) {
    restoreSlot(lp);
    resetClutch();
}

float NoFall::getProtectionFactor(SDK::Player* lp, std::chrono::steady_clock::time_point now) {
    if (protectionCachedAt != std::chrono::steady_clock::time_point {} &&
        now - protectionCachedAt < std::chrono::milliseconds(250)) {
        return cachedProtection;
    }
    cachedProtection = fallProtectionFactor(lp);
    protectionCachedAt = now;
    return cachedProtection;
}

float NoFall::aimAtPoint(SDK::LocalPlayer* lp, Vec3 const& point) {
    Vec3 eye = lp->getPos();
    Vec3 dir = point - eye;
    float dist = dir.magnitude();
    if (dist < 0.01f) return dist;

    Vec3 n = dir * (1.f / dist);
    Vec2 desired {
        std::clamp(-std::asin(std::clamp(n.y, -1.f, 1.f)) * (180.f / pi_f), -89.9f, 89.9f),
        std::atan2(n.z, n.x) * (180.f / pi_f) - 90.f,
    };

    Vec2 current = lp->getRot();
    Vec2 error { desired.x - current.x, wrapAngle(desired.y - current.y) };
    if (std::abs(error.x) > 0.05f || std::abs(error.y) > 0.05f) {
        lp->applyTurnDelta(Vec2 { -error.x, error.y });
    }
    return dist;
}

float NoFall::aimAtWater(SDK::LocalPlayer* lp) {
    Vec3 eye = lp->getPos();
    Vec3 target { static_cast<float>(placedWaterPos.x) + 0.5f, static_cast<float>(placedWaterPos.y) + 0.5f,
                  static_cast<float>(placedWaterPos.z) + 0.5f };
    Vec3 dir = target - eye;
    float dist = dir.magnitude();
    if (dist < 0.01f) return dist;

    Vec3 n = dir * (1.f / dist);
    Vec2 desired {
        std::clamp(-std::asin(std::clamp(n.y, -1.f, 1.f)) * (180.f / pi_f), -89.9f, 89.9f),
        std::atan2(n.z, n.x) * (180.f / pi_f) - 90.f,
    };

    Vec2 current = lp->getRot();
    Vec2 error { desired.x - current.x, wrapAngle(desired.y - current.y) };
    if (std::abs(error.x) > 0.05f || std::abs(error.y) > 0.05f) {
        lp->applyTurnDelta(Vec2 { -error.x, error.y });
    }
    return dist;
}

bool NoFall::runPickup(SDK::LocalPlayer* lp, std::chrono::steady_clock::time_point now) {
    if (pickupStartedAt == std::chrono::steady_clock::time_point {}) pickupStartedAt = now;

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - pickupStartedAt).count();

    auto ci = SDK::ClientInstance::get();
    auto* region = ci ? ci->getRegion() : nullptr;

    if (pickupClickedAt != std::chrono::steady_clock::time_point {}) {
        auto heldMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - pickupClickedAt).count();
        if (region && !MovementSim::isLiquidAt(region, placedWaterPos)) {
            return true;
        }
        auto mouse = SDK::MouseDevice::get();
        bool queueEmpty = !mouse || mouse->inputs.empty();
        if (queueEmpty && heldMs >= 120) {
            pickupClickedAt = {};
            return false;
        }
        if (heldMs >= 1500) {
            return true;
        }
        aimAtWater(lp);
        return false;
    }

    if (elapsedMs > 3000) return true;

    if (!region) return true;

    if (!MovementSim::isLiquidAt(region, placedWaterPos)) return true;

    float dist = aimAtWater(lp);
    if (dist > 4.5f) return true;

    int bucketSlot = findEmptyBucketSlot(lp);
    if (bucketSlot < 0) return true;

    if (lp->supplies->selectedSlot != bucketSlot) {
        lp->supplies->selectedSlot = bucketSlot;
        return false;
    }

    auto level = ci->minecraft ? ci->minecraft->getLevel() : nullptr;
    auto liquidHit = level ? level->getLiquidHitResult() : nullptr;
    bool liquidAimed = liquidHit && liquidHit->hitType != SDK::HitType::AIR;
    if (!liquidAimed) {
        if (elapsedMs < 800) return false;
        return true;
    }

    if (!SDK::MouseDevice::get()) return true;

    pushAction(2, true);
    pushAction(2, false);
    pickupClickedAt = now;
    return false;
}

bool NoFall::maceLockout(SDK::Player* lp) {
    if (!std::get<BoolValue>(disableWithMace)) return false;
    if (MaceUtil::isHoldingMace(lp)) return true;

    return false;
}

void NoFall::onTick(Event&) {
    runClutch();
}

void NoFall::onUpdate(Event&) {
    runClutch();
}

void NoFall::runClutch() {
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft || !ci->minecraftGame) {
        stopChoke();
        return;
    }
    if (!ci->minecraftGame->isCursorGrabbed() ||
        Necromancer::get().getScreenManager().getActiveScreen()) {
        if (chokeActive) {
            auto lpNow = ci->getLocalPlayer();
            restoreSlot(lpNow);
            resetClutch();
        }
        return;
    }

    auto lp = ci->getLocalPlayer();
    if (!lp || !lp->supplies) {
        stopChoke();
        return;
    }

    auto now = std::chrono::steady_clock::now();

    if (mode.getSelectedKey() != 0) {
        abortClutch(lp, "mode changed");
        return;
    }

    if (lp->getStatusFlag(actorFlagGliding)) {
        abortClutch(lp, "gliding");
        return;
    }

    if (maceLockout(lp)) {
        abortClutch(lp, "mace");
        return;
    }

    if (state == ClutchState::ChokePre || state == ClutchState::ChokePost) {
        int wantTicks = static_cast<int>(std::get<FloatValue>(chokeTicks).value);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - chokeStartedAt).count();
        bool ticksDone = chokedPackets >= wantTicks || elapsed >= wantTicks * 50 + 200;

        aimAtPoint(lp, chokeAimPoint);

        if (state == ClutchState::ChokePre) {
            if (!ticksDone) return;
            stopChoke();
            allowOnePacket = true;
            state = ClutchState::ChokeRelease;
            chokeStartedAt = now;
            return;
        }

        auto* region = ci->getRegion();
        bool waterThere = region && MovementSim::isLiquidAt(region, placedWaterPos);
        if (waterThere || ticksDone) {
            stopChoke();
            if (waterThere && std::get<BoolValue>(pickUpWater)) {
                state = ClutchState::WaitLanding;
                landedAt = {};
                lastStateLog = {};
                placedAt = now;
                sawFallingAfterPlace = false;
            } else {
                restoreSlot(lp);
                resetClutch();
                cooldownUntil = now + std::chrono::milliseconds(600);
            }
        }
        return;
    }

    if (state == ClutchState::ChokeRelease) {
        aimAtPoint(lp, chokeAimPoint);

        auto level = ci->minecraft->getLevel();
        auto hit = level ? level->getHitResult() : nullptr;
        bool canPlace = hit && hit->hitType == SDK::HitType::BLOCK && hit->face == 1;
        auto waitedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - chokeStartedAt).count();

        if (!canPlace) {
            if (waitedMs < 200) return;
            Logger::Warn("[NoFall] choke release: no top face to place on, aborting");
            restoreSlot(lp);
            resetClutch();
            cooldownUntil = now + std::chrono::milliseconds(400);
            return;
        }

        if (clutchBucketSlot >= 0 && lp->supplies->selectedSlot != clutchBucketSlot) {
            lp->supplies->selectedSlot = clutchBucketSlot;
        }
        placedWaterPos = BlockPos { hit->hitBlock.x, hit->hitBlock.y + 1, hit->hitBlock.z };
        pushAction(2, true);
        pushAction(2, false);
        placeAttempts++;

        startChoke();
        chokeStartedAt = now;
        state = ClutchState::ChokePost;
        placedAt = now;
        fallDistance = 0.f;
        hasLastY = false;
        return;
    }

    if (state == ClutchState::Placed) {
        auto mouse = SDK::MouseDevice::get();
        bool queueEmpty = !mouse || mouse->inputs.empty();
        auto heldMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - placedAt).count();
        auto* region = ci->getRegion();
        bool waterThere = region && MovementSim::isLiquidAt(region, placedWaterPos);
        bool wantPickup = std::get<BoolValue>(pickUpWater);
        float velY = lp->getVelocity().y;
        bool stillFalling = velY < -0.08f && !lp->isOnGround();

        if (waterThere) {
            if (wantPickup) {
                state = ClutchState::WaitLanding;
                landedAt = {};
                lastStateLog = {};
                sawFallingAfterPlace = true;
            } else {
                restoreSlot(lp);
                resetClutch();
                cooldownUntil = now + std::chrono::milliseconds(600);
            }
            return;
        }

        if (stillFalling && queueEmpty && placeAttempts < 12) {
            float feet = lp->getBoundingBox().lower.y;
            float aboveTarget = feet - static_cast<float>(placeTargetBlock.y + 1);
            if (aboveTarget > -0.5f) {
                auto hit = ci->minecraft->getLevel() ? ci->minecraft->getLevel()->getHitResult() : nullptr;
                bool canPlace = hit && hit->hitType == SDK::HitType::BLOCK && hit->face == 1;
                if (canPlace) {
                    if (lp->supplies->selectedSlot != clutchBucketSlot && clutchBucketSlot >= 0) {
                        lp->supplies->selectedSlot = clutchBucketSlot;
                    }
                    pushAction(2, true);
                    pushAction(2, false);
                    placeAttempts++;
                    placedWaterPos = BlockPos { hit->hitBlock.x, hit->hitBlock.y + 1, hit->hitBlock.z };
                    return;
                }
            }
        }

        if (!stillFalling || heldMs >= 1200 || placeAttempts >= 12) {
            restoreSlot(lp);
            resetClutch();
            cooldownUntil = now + std::chrono::milliseconds(400);
        }
        return;
    }

    if (state == ClutchState::WaitLanding) {
        auto* region = ci->getRegion();
        if (region && !MovementSim::isLiquidAt(region, placedWaterPos)) {
            restoreSlot(lp);
            resetClutch();
            cooldownUntil = now + std::chrono::milliseconds(400);
            return;
        }

        aimAtWater(lp);

        bool onGround = lp->isOnGround();
        float velY = lp->getVelocity().y;
        auto waitedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - placedAt).count();

        if (waitedMs > 8000) {
            restoreSlot(lp);
            resetClutch();
            cooldownUntil = now + std::chrono::milliseconds(600);
            return;
        }

        if (!sawFallingAfterPlace) {
            if (onGround) {
                sawFallingAfterPlace = true;
            } else if (velY < -0.15f) {
                sawFallingAfterPlace = true;
            } else {
                auto sinceUnfreeze =
                    unfrozeAt == std::chrono::steady_clock::time_point {}
                        ? 0
                        : std::chrono::duration_cast<std::chrono::milliseconds>(now - unfrozeAt).count();
                if (sinceUnfreeze > 600) {
                    sawFallingAfterPlace = true;
                }
                return;
            }
        }

        bool stopped = velY > -0.08f;
        if (!onGround && !stopped) return;

        state = ClutchState::PickingUp;
        pickupStartedAt = {};
        pickupClickedAt = {};
        lastStateLog = {};
        return;
    }

    if (state == ClutchState::PickingUp) {
        if (runPickup(lp, now)) {
            restoreSlot(lp);
            resetClutch();
            fallDistance = 0.f;
            hasLastY = false;
            cooldownUntil = now + std::chrono::milliseconds(600);
        }
        return;
    }

    if (lp->isOnGround()) {
        fallDistance = 0.f;
        hasLastY = false;
        abortClutch(lp, "on ground");
        return;
    }

    float posY = lp->getPos().y;
    if (!hasLastY) {
        lastY = posY;
        hasLastY = true;
    }
    float descent = lastY - posY;
    if (descent > 0.f) fallDistance += descent;
    else if (descent < -0.001f) fallDistance = 0.f;
    lastY = posY;

    float velY = lp->getVelocity().y;
    if (velY > 0.05f) {
        slowFallSince = {};
        abortClutch(lp, "moving up");
        return;
    }
    if (velY > -0.15f) {
        if (state == ClutchState::Aiming) {
            if (slowFallSince == std::chrono::steady_clock::time_point {}) {
                slowFallSince = now;
            } else if (now - slowFallSince > std::chrono::milliseconds(400)) {
                slowFallSince = {};
                abortClutch(lp, "fall interrupted");
                return;
            }
        } else {
            return;
        }
    } else {
        slowFallSince = {};
    }
    if (state == ClutchState::Idle && velY > -0.30f) return;

    int bucketSlot = findWaterBucketSlot(lp);
    if (bucketSlot < 0) {
        abortClutch(lp, "no water bucket");
        return;
    }

    if (now < cooldownUntil) return;

    auto landing = MovementSim::predictLanding(lp);
    if (!landing) {
        abortClutch(lp, "no landing found");
        return;
    }

    if (std::get<BoolValue>(ignorePlacedWater) && landing->landsInLiquid) {
        abortClutch(lp, "landing in water");
        return;
    }

    float feetY = lp->getBoundingBox().lower.y;
    float totalFall = fallDistance + std::max(0.f, feetY - landing->faceY);
    float predictedDamage = std::max(0.f, totalFall - safeFallBlocks) * getProtectionFactor(lp, now);
    float hp = lp->getHealth().value_or(20.f);
    bool thresholdHit = predictedDamage >= std::get<FloatValue>(minDamage).value;
    bool lethal = std::get<BoolValue>(forceWhenLow) && predictedDamage >= hp;
    if (!thresholdHit && !lethal) {
        abortClutch(lp, "damage below threshold");
        return;
    }

    Vec3 eye = lp->getPos();
    float faceCx = (landing->faceMin.x + landing->faceMax.x) * 0.5f;
    float faceCz = (landing->faceMin.y + landing->faceMax.y) * 0.5f;
    Vec3 aimPoint { faceCx, landing->faceY, faceCz };
    float heightAboveFace = eye.y - landing->faceY;
    int ticksLeft = landing->ticksToImpact;

    if (state == ClutchState::Idle) {
        float aimRange = std::get<FloatValue>(aimDistance).value;
        float brakingDist = std::max(6.f, -velY * 10.f);
        float trigger = std::max(aimRange, brakingDist);
        int armTicks = static_cast<int>(std::get<FloatValue>(armTickLead).value);
        if (heightAboveFace > trigger && ticksLeft > armTicks) return;

        state = ClutchState::Aiming;
        lastAimFrame = {};
        placeAttempts = 0;
        preSwitched = false;
    }

    if (!preSwitched) {
        float switchRange = std::get<FloatValue>(preSwitchDistance).value;
        float switchTrigger = std::max(switchRange, -velY * 8.f);
        if (heightAboveFace <= switchTrigger || ticksLeft <= 6) {
            originalSlot = lp->supplies->selectedSlot;
            clutchBucketSlot = bucketSlot;
            preSwitched = true;
            if (lp->supplies->selectedSlot != bucketSlot) {
                lp->supplies->selectedSlot = bucketSlot;
            }
        }
    } else if (lp->supplies->selectedSlot != bucketSlot) {
        lp->supplies->selectedSlot = bucketSlot;
    }

    Vec3 dir = aimPoint - eye;
    float dist = dir.magnitude();
    float horiz = std::hypot(dir.x, dir.z);
    Vec2 current = lp->getRot();
    Vec2 desired;
    if (horiz < 0.01f || dist < 0.01f) {
        desired = Vec2 { 89.9f, current.y };
    } else {
        Vec3 n = dir * (1.f / dist);
        desired = Vec2 {
            -std::asin(std::clamp(n.y, -1.f, 1.f)) * (180.f / pi_f),
            std::atan2(n.z, n.x) * (180.f / pi_f) - 90.f,
        };
        desired.x = std::clamp(desired.x, -89.9f, 89.9f);
    }

    Vec2 error { desired.x - current.x, wrapAngle(desired.y - current.y) };

    if (lastAimFrame == std::chrono::steady_clock::time_point {}) lastAimFrame = now;
    float dt = std::clamp(std::chrono::duration<float>(now - lastAimFrame).count(), 0.001f, 0.05f);
    lastAimFrame = now;

    float alpha = 1.f;
    float maxStep = 10000.f * dt;
    Vec2 step { std::clamp(error.x * alpha, -maxStep, maxStep), std::clamp(error.y * alpha, -maxStep, maxStep) };
    if (std::abs(step.x) > 0.001f || std::abs(step.y) > 0.001f) {
        lp->applyTurnDelta(Vec2 { -step.x, step.y });
    }

    float reachMax = std::get<FloatValue>(placeReach).value;
    float reachSafe = std::max(2.5f, reachMax - 0.4f);

    float nextVelY = advanceVelY(velY);
    float nextHeight = heightAboveFace + nextVelY;
    float nextDist = dist + nextVelY;
    float thenVelY = advanceVelY(nextVelY);
    float thenDist = nextDist + thenVelY;

    float angTol = std::clamp(std::atan2(0.42f, std::max(0.5f, dist)) * (180.f / pi_f), 2.5f, 20.f);

    bool inReach = dist <= reachMax;
    bool aimTight = std::abs(error.x) <= angTol && std::abs(error.y) <= angTol;
    bool wouldOvershoot = nextHeight <= 1.5f || nextDist <= reachSafe || thenDist <= reachSafe || ticksLeft <= 2;
    if (!inReach) return;
    if (!aimTight && !wouldOvershoot) return;

    if (std::get<BoolValue>(useFakelag)) {
        chokeAimPoint = aimPoint;
        placeTargetBlock = landing->landingBlock;
        if (originalSlot < 0) originalSlot = lp->supplies->selectedSlot;
        clutchBucketSlot = bucketSlot;
        startChoke();
        chokeStartedAt = now;
        state = ClutchState::ChokePre;
        return;
    }

    auto level = ci->minecraft->getLevel();
    auto hit = level ? level->getHitResult() : nullptr;
    bool hitIsBlock = hit && hit->hitType == SDK::HitType::BLOCK;
    bool sameBlock = hitIsBlock && hit->hitBlock.x == landing->landingBlock.x &&
                     hit->hitBlock.y == landing->landingBlock.y && hit->hitBlock.z == landing->landingBlock.z;
    bool hitMatches = sameBlock && hit->face == 1;
    bool fallbackTopFace = hitIsBlock && hit->face == 1 && (wouldOvershoot || aimTight);
    if (!hitMatches && !fallbackTopFace) return;

    auto mouse = SDK::MouseDevice::get();
    if (!mouse) return;

    if (originalSlot < 0) originalSlot = lp->supplies->selectedSlot;
    clutchBucketSlot = bucketSlot;
    placedWaterPos = BlockPos { hit->hitBlock.x, hit->hitBlock.y + 1, hit->hitBlock.z };
    placeTargetBlock = hit->hitBlock;
    if (lp->supplies->selectedSlot != bucketSlot) lp->supplies->selectedSlot = bucketSlot;
    pushAction(2, true);
    pushAction(2, false);
    state = ClutchState::Placed;
    placedAt = now;
    placeAttempts = 1;
    fallDistance = 0.f;
    hasLastY = false;
}
