#include "pch.h"
#include "Fakelag.h"
#include "client/event/events/SendPacketEvent.h"
#include "client/event/events/PacketReceiveEvent.h"
#include "client/event/events/AttackEvent.h"
#include "client/event/events/ClickEvent.h"
#include "client/event/events/UpdateEvent.h"
#include "client/event/events/RenderLevelEvent.h"
#include "client/misc/EntityCache.h"
#include "client/misc/WallCheck.h"
#include "client/feature/module/modules/visual/AntiObs.h"
#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/player/LocalPlayer.h"
#include "mc/common/world/level/Level.h"
#include "mc/common/world/level/HitResult.h"
#include "mc/common/network/packet/SetActorMotionPacket.h"
#include "mc/common/network/packet/ActorEventPacket.h"
#include <util/DrawUtil3D.h>

namespace {
    Vec3 lookDir(Vec2 const& rot) {
        float yaw = (rot.y + 90.f) * (pi_f / 180.f);
        float pitch = rot.x * -(pi_f / 180.f);
        return { cosf(yaw) * cosf(pitch), sinf(pitch), sinf(yaw) * cosf(pitch) };
    }
}

Fakelag::Fakelag()
    : Module("Fakelag", LocalizeString::get("client.module.fakelag.name"),
             LocalizeString::get("client.module.fakelag.desc"), GAME, nokeybind) {
    addSetting("suppressKnockback", LocalizeString::get("client.module.fakelag.suppressKnockback.name"),
               LocalizeString::get("client.module.fakelag.suppressKnockback.desc"), suppressKnockback);
    addSetting("expertSettings", LocalizeString::get("client.module.fakelag.expertSettings.name"),
               LocalizeString::get("client.module.fakelag.expertSettings.desc"), expertSettings);
    addSetting("adaptive", LocalizeString::get("client.module.fakelag.adaptive.name"),
               LocalizeString::get("client.module.fakelag.adaptive.desc"), adaptive, "expertSettings"_istrue);
    addSliderSetting("delayBetweenChokes", LocalizeString::get("client.module.fakelag.delayBetweenChokes.name"),
                     LocalizeString::get("client.module.fakelag.delayBetweenChokes.desc"), delayBetweenChokes,
                     FloatValue(0.1f), FloatValue(10.f), FloatValue(0.1f));
    addSliderSetting("maxTime", LocalizeString::get("client.module.fakelag.maxTime.name"),
                     LocalizeString::get("client.module.fakelag.maxTime.desc"), maxTime, FloatValue(0.f),
                     FloatValue(10.f), FloatValue(0.1f), "expertSettings"_istrue);
    addSliderSetting("minRange", LocalizeString::get("client.module.fakelag.minRange.name"),
                     LocalizeString::get("client.module.fakelag.minRange.desc"), minRange, FloatValue(0.f),
                     FloatValue(10.f), FloatValue(0.5f), "expertSettings"_istrue);
    addSliderSetting("maxRange", LocalizeString::get("client.module.fakelag.maxRange.name"),
                     LocalizeString::get("client.module.fakelag.maxRange.desc"), maxRange, FloatValue(0.f),
                     FloatValue(10.f), FloatValue(0.5f), "expertSettings"_istrue);
    addSetting("randomizeRange", LocalizeString::get("client.module.fakelag.randomizeRange.name"),
               LocalizeString::get("client.module.fakelag.randomizeRange.desc"), randomizeRange,
               "expertSettings"_istrue);
    addSetting("unchokeOnHit", LocalizeString::get("client.module.fakelag.unchokeOnHit.name"),
               LocalizeString::get("client.module.fakelag.unchokeOnHit.desc"), unchokeOnHit);
    addSetting("riskyHeightChange", LocalizeString::get("client.module.fakelag.riskyHeightChange.name"),
               LocalizeString::get("client.module.fakelag.riskyHeightChange.desc"), riskyHeightChange);
    addSetting("unchokeAtCorners", LocalizeString::get("client.module.fakelag.unchokeAtCorners.name"),
               LocalizeString::get("client.module.fakelag.unchokeAtCorners.desc"), unchokeAtCorners);
    addSliderSetting("cornerThickness", LocalizeString::get("client.module.fakelag.cornerThickness.name"),
                     LocalizeString::get("client.module.fakelag.cornerThickness.desc"), cornerThickness,
                     FloatValue(0.f), FloatValue(1.5f), FloatValue(0.05f), "unchokeAtCorners"_istrue);
    addSliderSetting("chokeWhenDamaged", LocalizeString::get("client.module.fakelag.chokeWhenDamaged.name"),
                     LocalizeString::get("client.module.fakelag.chokeWhenDamaged.desc"), chokeWhenDamaged,
                     FloatValue(0.f), FloatValue(2.f), FloatValue(0.1f));
    addSetting("onlyEnemyHit", LocalizeString::get("client.module.fakelag.onlyEnemyHit.name"),
               LocalizeString::get("client.module.fakelag.onlyEnemyHit.desc"), onlyEnemyHit);
    addSetting("unchokeOnBuild", LocalizeString::get("client.module.fakelag.unchokeOnBuild.name"),
               LocalizeString::get("client.module.fakelag.unchokeOnBuild.desc"), unchokeOnBuild);
    addSetting("unchokeOnBreak", LocalizeString::get("client.module.fakelag.unchokeOnBreak.name"),
               LocalizeString::get("client.module.fakelag.unchokeOnBreak.desc"), unchokeOnBreak);
    addSetting("showChokedHitbox", LocalizeString::get("client.module.fakelag.showChokedHitbox.name"),
               LocalizeString::get("client.module.fakelag.showChokedHitbox.desc"), showChokedHitbox);
    addSetting("chokedHitboxColor", LocalizeString::get("client.module.fakelag.chokedHitboxColor.name"),
               LocalizeString::get("client.module.fakelag.chokedHitboxColor.desc"), chokedHitboxColor,
               "showChokedHitbox"_istrue);
    addSliderSetting("chokedHitboxThickness", LocalizeString::get("client.module.fakelag.chokedHitboxThickness.name"),
                     LocalizeString::get("client.module.fakelag.chokedHitboxThickness.desc"), chokedHitboxThickness,
                     FloatValue(0.1f), FloatValue(1.f), FloatValue(0.1f), "showChokedHitbox"_istrue);

    listen<UpdateEvent>((EventListenerFunc)&Fakelag::onUpdate);
    listen<SendPacketEvent>((EventListenerFunc)&Fakelag::onSendPacket);
    listen<PacketReceiveEvent>((EventListenerFunc)&Fakelag::onPacketReceive);
    listen<AttackEvent>((EventListenerFunc)&Fakelag::onAttack);
    listen<ClickEvent>((EventListenerFunc)&Fakelag::onClick);
    Eventing::get().listen<RenderLevelEvent, &Fakelag::onRenderLevel>(this);
}

void Fakelag::onEnable() {
    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (lp) captureChokeState(lp); else { chokeOrigin = {}; chokeBoxLower = {}; chokeBoxHigher = {}; }
    chokeStart = std::chrono::steady_clock::now();
    lastHitAt = {};
    lastEnemySwingAt = {};
    gapUntil = {};
    hitPending = false;
    releasePending = false;
    gapPhase = false;
    damageChokeActive = false;
    kbBankedAt = {};
    heldKnockback = Vec3 {};
    hasHeldKnockback = false;
    wasDamageOnly = damageOnlyMode();
    rollRangeThreshold();

    breakProgressSeen = 0.f;
}

bool Fakelag::damageOnlyMode() {
    return !std::get<BoolValue>(expertSettings) && std::get<FloatValue>(chokeWhenDamaged).value > 0.f;
}

void Fakelag::onUpdate(Event&) {
    if (releasePending) return;

    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (!lp) return;

    auto now = std::chrono::steady_clock::now();

    bool damageOnly = damageOnlyMode();
    if (damageOnly != wasDamageOnly) {
        wasDamageOnly = damageOnly;
        releasePending = false;
        hitPending = false;
        gapPhase = false;
        damageChokeActive = false;
        if (!damageOnly) {
            captureChokeState(lp);
            chokeStart = now;
        }
    }

    if (damageOnly) {
        if (!damageChokeActive) {
            if (hasHeldKnockback && std::chrono::duration<float>(now - kbBankedAt).count() > 0.3f) {
                flushKnockback();
            }
            return;
        }

        if (std::get<BoolValue>(unchokeAtCorners) && wallBetweenGhostAndPlayer(lp)) {
            releasePending = true;
            hitPending = false;
            return;
        }

        if (std::get<BoolValue>(unchokeOnBreak) && isBreakingBlock(lp)) {
            releasePending = true;
            hitPending = false;
            return;
        }

        float delay = std::get<FloatValue>(chokeWhenDamaged).value;
        if (std::chrono::duration<float>(now - lastHitAt).count() >= delay) {
            releasePending = true;
            hitPending = false;
        }
        return;
    }

    if (gapPhase) {
        if (now < gapUntil) return;
        gapPhase = false;
        captureChokeState(lp);
        chokeStart = now;
        rollRangeThreshold();
    }

    auto triggerRelease = [&] {
        releasePending = true;
        hitPending = false;
    };

    bool expert = std::get<BoolValue>(expertSettings);
    float minRangeVal = std::get<FloatValue>(minRange).value;
    float maxRangeVal = std::get<FloatValue>(maxRange).value;
    float maxTimeVal = expert ? std::get<FloatValue>(maxTime).value : 0.f;
    float movedDist = lp->getPos().distance(chokeOrigin);

    if (maxTimeVal > 0.f && std::chrono::duration<float>(now - chokeStart).count() >= maxTimeVal) {
        triggerRelease();
        return;
    }

    if (expert && (minRangeVal != 0.f || maxRangeVal != 0.f)) {
        float rangeLimit = std::get<BoolValue>(randomizeRange) ? rangeThreshold : maxRangeVal;
        if (movedDist >= rangeLimit) {
            triggerRelease();
            return;
        }
    }

    if (std::get<BoolValue>(riskyHeightChange) && std::abs(lp->getPos().y - chokeOrigin.y) >= 2.f) {
        triggerRelease();
        return;
    }

    if (std::get<BoolValue>(unchokeAtCorners) && wallBetweenGhostAndPlayer(lp)) {
        triggerRelease();
        return;
    }

    if (std::get<BoolValue>(unchokeOnBreak) && isBreakingBlock(lp)) {
        triggerRelease();
        return;
    }

    if (hitPending) {
        float delay = std::get<FloatValue>(chokeWhenDamaged).value;
        if (std::chrono::duration<float>(now - lastHitAt).count() >= delay) {
            triggerRelease();
            return;
        }
    }

    if (expert && std::get<BoolValue>(adaptive)) {
        float adaptiveFloor = std::min(minRangeVal, maxRangeVal);
        if (movedDist >= adaptiveFloor && enemyWatchingGhost(lp)) {
            triggerRelease();
        }
    }
}

void Fakelag::onSendPacket(Event& evG) {
    auto& ev = reinterpret_cast<SendPacketEvent&>(evG);
    auto* packet = ev.getPacket();
    if (!packet) return;

    auto id = packet->getID();
    if (id != SDK::PacketID::PLAYER_AUTH_INPUT && id != SDK::PacketID::MOVE_PLAYER) return;

    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (!lp) return;

    if (releasePending) {
        releasePending = false;
        if (damageOnlyMode()) {
            damageChokeActive = false;
        } else {
            gapPhase = true;
            gapUntil =
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds(static_cast<int>(std::get<FloatValue>(delayBetweenChokes).value * 1000.f));
        }
        flushKnockback();
        return;
    }

    if (damageOnlyMode() && !damageChokeActive) return;

    if (gapPhase) return;

    ev.setCancelled(true);
}

void Fakelag::onPacketReceive(Event& evG) {
    auto& ev = reinterpret_cast<PacketReceiveEvent&>(evG);
    auto* packet = ev.getPacket();
    if (!packet) return;

    auto id = packet->getID();
    if (id != SDK::PacketID::SET_ENTITY_MOTION && id != SDK::PacketID::ACTOR_EVENT) return;

    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (!lp) return;

    if (id == SDK::PacketID::ACTOR_EVENT) {
        auto* eventPacket = static_cast<SDK::ActorEventPacket*>(packet);
        auto now = std::chrono::steady_clock::now();

        if (eventPacket->eventID == SDK::ActorEventID::ARM_SWING) {
            if (eventPacket->runtimeID != lp->getRuntimeID() && isPlayerRuntimeID(eventPacket->runtimeID)) {
                lastEnemySwingAt = now;
            }
            return;
        }

        if (eventPacket->eventID != SDK::ActorEventID::HURT_ANIMATION ||
            eventPacket->runtimeID != lp->getRuntimeID())
            return;

        float chokeDelay = std::get<FloatValue>(chokeWhenDamaged).value;
        if (chokeDelay <= 0.f) return;

        lastHitAt = now;
        hitPending = true;

        if (damageOnlyMode()) {
            if (!damageChokeActive) {
                damageChokeActive = true;
                captureChokeState(lp);
                chokeStart = now;
            }
        } else if (gapPhase) {
            gapPhase = false;
            captureChokeState(lp);
            chokeStart = now;
            rollRangeThreshold();
        }
        return;
    }

    auto* motionPacket = static_cast<SDK::SetActorMotionPacket*>(packet);
    if (motionPacket->getRuntimeID() != lp->getRuntimeID()) return;

    if (!std::get<BoolValue>(suppressKnockback)) return;

    bool chokingNow = damageOnlyMode() ? damageChokeActive : (!gapPhase && !releasePending);
    if (!chokingNow) {
        if (!damageOnlyMode()) return;
        if (!hasHeldKnockback) kbBankedAt = std::chrono::steady_clock::now();
    }

    Vec3 motion = motionPacket->getMotion();
    heldKnockback = Vec3 { heldKnockback.x + motion.x, heldKnockback.y + motion.y, heldKnockback.z + motion.z };
    hasHeldKnockback = true;
    ev.setCancelled(true);
}

void Fakelag::onAttack(Event&) {
    if (!std::get<BoolValue>(unchokeOnHit)) return;

    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->getLocalPlayer()) return;

    releasePending = true;
    hitPending = false;
}

void Fakelag::onClick(Event& evG) {
    if (!std::get<BoolValue>(unchokeOnBuild)) return;

    auto& ev = reinterpret_cast<ClickEvent&>(evG);
    if (ev.getClickType() != ClickEvent::ClickType::Right || !ev.isDown()) return;

    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (!lp || !ci->minecraft) return;

    auto level = ci->minecraft->getLevel();
    auto hit = level ? level->getHitResult() : nullptr;
    if (!hit || hit->hitType != SDK::HitType::BLOCK) return;

    AABB& bb = lp->getBoundingBox();
    if (hit->hitBlock.y < static_cast<int>(std::floor(bb.lower.y)) - 1) return;

    releasePending = true;
    hitPending = false;
}

bool Fakelag::isBreakingBlock(SDK::LocalPlayer* lp) {
    if (!lp->gameMode) return false;

    float progress = lp->gameMode->breakProgress;
    bool breaking = progress > 0.f || (breakProgressSeen > 0.f && progress == 0.f);
    breakProgressSeen = progress;
    return breaking;
}

bool Fakelag::wallBetweenGhostAndPlayer(SDK::LocalPlayer* lp) {
    auto ci = SDK::ClientInstance::get();
    auto region = ci ? ci->getRegion() : nullptr;
    if (!region) return false;

    Vec3 ghostMid = (chokeBoxLower + chokeBoxHigher) * 0.5f;
    Vec3 playerMid = lp->getBoundingBox().getCenter();
    Vec3 delta = playerMid - ghostMid;
    if (delta.magnitude() < 0.05f) return false;

    float radius = std::clamp(std::get<FloatValue>(cornerThickness).value, 0.f, 1.5f);
    float halfHeight = std::max(0.1f, (chokeBoxHigher.y - chokeBoxLower.y) * 0.5f);

    float heights[5] = {
        -halfHeight + 0.05f, -halfHeight * 0.5f, 0.f, halfHeight * 0.5f, halfHeight - 0.05f,
    };
    float axis[3] = { -radius, 0.f, radius };

    WallCheck::beginPass();
    for (float ox : axis) {
        for (float oz : axis) {
            for (float h : heights) {
                Vec3 shift { ox, h, oz };
                if (!WallCheck::isVisible(region, ghostMid + shift, playerMid + shift, 0.f)) return true;
            }
        }
    }

    return false;
}

void Fakelag::rollRangeThreshold() {
    float mn = std::min(std::get<FloatValue>(minRange).value, std::get<FloatValue>(maxRange).value);
    float mx = std::max(std::get<FloatValue>(minRange).value, std::get<FloatValue>(maxRange).value);
    std::uniform_real_distribution<float> dist(mn, mx);
    rangeThreshold = dist(rng);
}

bool Fakelag::isPlayerRuntimeID(uint64_t runtimeID) {
    auto* actor = EntityCache::get().findByRuntimeID(runtimeID);
    return actor && actor->isPlayer();
}

bool Fakelag::enemyWatchingGhost(SDK::LocalPlayer* lp) {
    AABB ghost { { chokeOrigin.x - 0.3f, chokeOrigin.y, chokeOrigin.z - 0.3f },
                 { chokeOrigin.x + 0.3f, chokeOrigin.y + 1.8f, chokeOrigin.z + 0.3f } };

    auto snap = EntityCache::get().snapshot();
    for (auto* actor : snap->actors) {
        if (!actor || actor == lp || !actor->isPlayer()) continue;

        auto health = actor->getHealth();
        if (!health || *health <= 0.f) continue;

        Vec3 pos = actor->getPos();
        if (pos.distance(chokeOrigin) > 24.f) continue;

        Vec3 eye { pos.x, pos.y + 1.62f, pos.z };
        Vec3 dir = lookDir(actor->getRot());
        if (ghost.intersectsRay(eye, dir, 24.f, 0.1f).has_value()) return true;
    }

    return false;
}

void Fakelag::onRenderLevel(RenderLevelEvent&) {
    if (AntiObs::isActive()) return;
    if (!std::get<BoolValue>(showChokedHitbox) || gapPhase) return;
    if (damageOnlyMode() && !damageChokeActive) return;

    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->levelRenderer || !SDK::ScreenContext::instance3d) return;
    if (!ci->getLocalPlayer()) return;

    d2d::Color fillCol(std::get<ColorValue>(chokedHitboxColor).getMainColor());
    d2d::Color lineCol = fillCol;
    lineCol.a = std::max(lineCol.a, 0.9f);
    float thickness = std::get<FloatValue>(chokedHitboxThickness).value / 10.f;

    MCDrawUtil3D dc(ci->levelRenderer, SDK::ScreenContext::instance3d, SDK::MaterialPtr::getUIColor());

    Vec3 l = chokeBoxLower;
    Vec3 h = chokeBoxHigher;

    dc.fillQuad({ l.x, l.y, l.z }, { h.x, l.y, l.z }, { h.x, l.y, h.z }, { l.x, l.y, h.z }, fillCol);
    dc.fillQuad({ l.x, h.y, l.z }, { h.x, h.y, l.z }, { h.x, h.y, h.z }, { l.x, h.y, h.z }, fillCol);
    dc.fillQuad({ l.x, l.y, l.z }, { h.x, l.y, l.z }, { h.x, h.y, l.z }, { l.x, h.y, l.z }, fillCol);
    dc.fillQuad({ l.x, l.y, h.z }, { h.x, l.y, h.z }, { h.x, h.y, h.z }, { l.x, h.y, h.z }, fillCol);
    dc.fillQuad({ l.x, l.y, l.z }, { l.x, l.y, h.z }, { l.x, h.y, h.z }, { l.x, h.y, l.z }, fillCol);
    dc.fillQuad({ h.x, l.y, l.z }, { h.x, l.y, h.z }, { h.x, h.y, h.z }, { h.x, h.y, l.z }, fillCol);

    std::pair<Vec3, Vec3> edges[12] = {
        { { l.x, l.y, l.z }, { h.x, l.y, l.z } }, { { h.x, l.y, l.z }, { h.x, l.y, h.z } },
        { { h.x, l.y, h.z }, { l.x, l.y, h.z } }, { { l.x, l.y, h.z }, { l.x, l.y, l.z } },
        { { l.x, h.y, l.z }, { h.x, h.y, l.z } }, { { h.x, h.y, l.z }, { h.x, h.y, h.z } },
        { { h.x, h.y, h.z }, { l.x, h.y, h.z } }, { { l.x, h.y, h.z }, { l.x, h.y, l.z } },
        { { l.x, l.y, l.z }, { l.x, h.y, l.z } }, { { h.x, l.y, l.z }, { h.x, h.y, l.z } },
        { { h.x, l.y, h.z }, { h.x, h.y, h.z } }, { { l.x, l.y, h.z }, { l.x, h.y, h.z } },
    };
    for (auto& e : edges) {
        dc.drawThickLine(e.first, e.second, thickness, lineCol);
    }
}

void Fakelag::captureChokeState(SDK::LocalPlayer* lp) {
    chokeOrigin = lp->getPos();
    AABB& bb = lp->getBoundingBox();
    float hw = (bb.higher.x - bb.lower.x) * 0.5f;
    float h  = bb.higher.y - bb.lower.y;
    float eyeOff = chokeOrigin.y - bb.lower.y;
    float feetY = chokeOrigin.y - eyeOff;
    chokeBoxLower  = { chokeOrigin.x - hw, feetY,     chokeOrigin.z - hw };
    chokeBoxHigher = { chokeOrigin.x + hw, feetY + h, chokeOrigin.z + hw };
}

void Fakelag::flushKnockback() {
    if (!hasHeldKnockback) return;

    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (lp) {
        Vec3& vel = lp->getVelocity();
        vel = Vec3 { vel.x + heldKnockback.x, vel.y + heldKnockback.y, vel.z + heldKnockback.z };
    }

    heldKnockback = Vec3 {};
    hasHeldKnockback = false;
}

void Fakelag::onDisable() {
    releasePending = false;
    hitPending = false;
    gapPhase = false;
    damageChokeActive = false;
    flushKnockback();
}
