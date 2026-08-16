#include "pch.h"
#include "AfterTrack.h"
#include "client/event/events/UpdateEvent.h"
#include "client/event/events/AveragePingEvent.h"
#include "client/event/events/AttackEvent.h"
#include "client/event/events/SendPacketEvent.h"
#include "client/event/events/LeaveGameEvent.h"
#include "client/event/events/RenderLevelEvent.h"
#include "client/feature/module/modules/visual/AntiObs.h"
#include "client/Necromancer.h"
#include "client/misc/EntityCache.h"
#include "client/misc/MovementSim.h"
#include "client/screen/ScreenManager.h"
#include "mc/Addresses.h"
#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/game/MinecraftGame.h"
#include "mc/common/client/player/LocalPlayer.h"
#include "mc/common/world/Minecraft.h"
#include "mc/common/world/level/Level.h"
#include "mc/common/world/actor/Actor.h"
#include "mc/common/network/RakNetConnector.h"
#include "mc/common/network/MinecraftPackets.h"
#include "mc/common/network/PacketSender.h"
#include <util/DrawUtil3D.h>
#include <cmath>
#include <unordered_set>

AfterTrack::AfterTrack()
    : Module("AfterTrack", LocalizeString::get("client.module.afterTrack.name"),
             LocalizeString::get("client.module.afterTrack.desc"), GAME, nokeybind) {
    auto simSet = addSliderSetting("maxSim", LocalizeString::get("client.module.afterTrack.maxSim.name"),
                     LocalizeString::get("client.module.afterTrack.maxSim.desc"), maxSim, FloatValue(0.f),
                     FloatValue(300.f), FloatValue(1.f));
    simSet->floatEditMax = static_cast<float>(maxSimCeilingMs);

    addSetting("useStackLatency", LocalizeString::get("client.module.afterTrack.useStackLatency.name"),
               LocalizeString::get("client.module.afterTrack.useStackLatency.desc"), useStackLatency);
    addSetting("hitbox", LocalizeString::get("client.module.afterTrack.hitbox.name"),
               LocalizeString::get("client.module.afterTrack.hitbox.desc"), hitbox);
    addSetting("hitboxColor", LocalizeString::get("client.module.afterTrack.hitboxColor.name"),
               LocalizeString::get("client.module.afterTrack.hitboxColor.desc"), hitboxColor, "hitbox"_istrue);
    hitboxStyle.addEntry(EnumEntry(style_outline, LocalizeString::get("client.module.afterTrack.hitboxStyle.outline.name"),
                                   LocalizeString::get("client.module.afterTrack.hitboxStyle.outline.desc")));
    hitboxStyle.addEntry(EnumEntry(style_filled, LocalizeString::get("client.module.afterTrack.hitboxStyle.filled.name"),
                                   LocalizeString::get("client.module.afterTrack.hitboxStyle.filled.desc")));
    hitboxStyle.addEntry(EnumEntry(style_both, LocalizeString::get("client.module.afterTrack.hitboxStyle.both.name"),
                                   LocalizeString::get("client.module.afterTrack.hitboxStyle.both.desc")));
    addEnumSetting("hitboxStyle", LocalizeString::get("client.module.afterTrack.hitboxStyle.name"),
                   LocalizeString::get("client.module.afterTrack.hitboxStyle.desc"), hitboxStyle, "hitbox"_istrue);
    Setting::Condition outlineCondition(std::vector<Setting::SingleCond> {
        { "hitbox", { 1 }, false },
        { "hitboxStyle", { style_outline, style_both }, false },
    });
    addSliderSetting("hitboxThickness", LocalizeString::get("client.module.afterTrack.hitboxThickness.name"),
                     LocalizeString::get("client.module.afterTrack.hitboxThickness.desc"), hitboxThickness,
                     FloatValue(0.1f), FloatValue(1.f), FloatValue(0.1f), outlineCondition);

    this->listen<UpdateEvent>(&AfterTrack::onUpdate);
    this->listen<AveragePingEvent>(&AfterTrack::onAvgPing, true);
    this->listen<AttackEvent>(&AfterTrack::onAttack);
    this->listen<SendPacketEvent>(&AfterTrack::onSendPacket);
    this->listen<LeaveGameEvent>(&AfterTrack::onLeaveGame);
    Eventing::get().listen<RenderLevelEvent, &AfterTrack::onRenderLevel>(this);
}

void AfterTrack::clearState() {
    velHistory.clear();
    predictions.clear();
    predValid.clear();
    lastPingMs = 0;
    pingKnown = false;
    nextPredictionAt = {};
    pendingAttackRID = 0;
    probeInFlight = false;
}

void AfterTrack::onEnable() {
    clearState();
}

void AfterTrack::onDisable() {
    clearState();
}

void AfterTrack::onLeaveGame(Event&) {
    clearState();
}

void AfterTrack::onAvgPing(Event& evG) {
    auto& ev = reinterpret_cast<AveragePingEvent&>(evG);
    lastPingMs = ev.getPing();
    pingKnown = true;
}

float AfterTrack::effectiveSimMs() const {
    if (!pingKnown || lastPingMs <= 0) return 0.f;
    float cap = std::clamp(std::get<FloatValue>(maxSim).value, 0.f, static_cast<float>(maxSimCeilingMs));
    return std::clamp(static_cast<float>(lastPingMs), 0.f, cap);
}

Vec3 AfterTrack::lookDirFromRot(Vec2 rot) const {
    float yaw = (rot.y + 90.f) * (pi_f / 180.f);
    float pitch = rot.x * -(pi_f / 180.f);
    return { cosf(yaw) * cosf(pitch), 0.f, sinf(yaw) * cosf(pitch) };
}

Vec3 AfterTrack::blendedVelocity(uint64_t rid, Vec3 liveVel) const {
    auto it = velHistory.find(rid);
    if (it == velHistory.end() || it->second.empty()) return liveVel;

    Vec3 trend {};
    int count = 0;
    for (auto const& r : it->second) {
        trend = trend + r.vel;
        count++;
    }
    if (count > 0) {
        trend = trend * (1.f / static_cast<float>(count));
    }

    Vec3 diff = liveVel - trend;
    float jitter = diff.magnitude();
    if (jitter < 0.05f) return liveVel;

    return Vec3 { liveVel.x * 0.7f + trend.x * 0.3f,
                  liveVel.y * 0.7f + trend.y * 0.3f,
                  liveVel.z * 0.7f + trend.z * 0.3f };
}

bool AfterTrack::samplePredictions() {
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return false;
    auto lp = ci->getLocalPlayer();
    if (!lp) return false;

    float simMs = effectiveSimMs();
    int ticks = static_cast<int>(std::round(simMs / tickStepMs));
    if (ticks <= 0) {
        predictions.clear();
        predValid.clear();
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    std::unordered_set<uint64_t> seen;

    auto snap = EntityCache::get().snapshot();
    seen.reserve(snap->views.size());
    for (auto* actor : snap->actors) {
        if (!actor || actor == lp || !actor->isPlayer()) continue;
        auto hp = actor->getHealth();
        if (!hp || *hp <= 0.f) continue;

        uint64_t rid = actor->getRuntimeID();
        seen.insert(rid);

        Vec3 liveVel = actor->getPos() - actor->getPosOld();
        auto& hist = velHistory[rid];
        hist.push_back({ now, liveVel });
        while (hist.size() > velHistoryLen) hist.pop_front();

        Vec3 baseVel = blendedVelocity(rid, liveVel);

        float speed = std::sqrt(baseVel.x * baseVel.x + baseVel.z * baseVel.z);
        Vec3 seedVel = baseVel;
        if (speed > 0.001f) {
            Vec3 look = lookDirFromRot(actor->getRot());
            Vec3 horiz { baseVel.x, 0.f, baseVel.z };
            float hlen = std::sqrt(horiz.x * horiz.x + horiz.z * horiz.z);
            if (hlen > 0.001f) {
                Vec3 horizDir { horiz.x / hlen, 0.f, horiz.z / hlen };
                Vec3 blendedDir {
                    horizDir.x * (1.f - fuzzyWeight) + look.x * fuzzyWeight,
                    0.f,
                    horizDir.z * (1.f - fuzzyWeight) + look.z * fuzzyWeight,
                };
                float blen = std::sqrt(blendedDir.x * blendedDir.x + blendedDir.z * blendedDir.z);
                if (blen > 0.001f) {
                    seedVel.x = blendedDir.x / blen * speed;
                    seedVel.z = blendedDir.z / blen * speed;
                }
            }
        }

        auto res = MovementSim::predictForward(actor, seedVel, ticks, false);
        if (res.valid) {
            predictions[rid] = res.finalBox;
            predValid[rid] = true;
        } else {
            predictions.erase(rid);
            predValid.erase(rid);
        }
    }

    std::erase_if(velHistory, [&](auto const& entry) { return !seen.contains(entry.first); });
    std::erase_if(predictions, [&](auto const& entry) { return !seen.contains(entry.first); });
    std::erase_if(predValid, [&](auto const& entry) { return !seen.contains(entry.first); });
    return true;
}

bool AfterTrack::getPredictedBox(uint64_t runtimeID, AABB& out) {
    if (!isEnabled()) return false;
    auto it = predictions.find(runtimeID);
    if (it == predictions.end()) return false;
    auto vit = predValid.find(runtimeID);
    if (vit == predValid.end() || !vit->second) return false;
    out = it->second;
    return true;
}

void AfterTrack::onUpdate(Event&) {
    auto now = std::chrono::steady_clock::now();
    if (now < nextPredictionAt) return;
    nextPredictionAt = now + std::chrono::milliseconds(static_cast<int>(tickStepMs));
    samplePredictions();
}

void AfterTrack::onAttack(Event& evG) {
    auto& ev = reinterpret_cast<AttackEvent&>(evG);
    auto* target = ev.getActor();
    if (!target) return;

    float simMs = effectiveSimMs();
    if (simMs <= 0.f) return;
    if (!std::get<BoolValue>(useStackLatency)) return;

    pendingAttackRID = target->getRuntimeID();
    pendingAttackAt = std::chrono::steady_clock::now();
}

void AfterTrack::onSendPacket(Event& evG) {
    auto& ev = reinterpret_cast<SendPacketEvent&>(evG);
    auto* packet = ev.getPacket();
    if (!packet) return;

    if (packet->getID() == SDK::PacketID::NETWORK_STACK_LATENCY) {
        if (probeInFlight) return;
        return;
    }

    uint64_t target = 0;
    if (!isAttackPacket(packet, target)) return;

    if (!std::get<BoolValue>(useStackLatency)) return;
    if (pendingAttackRID != target) return;

    auto now = std::chrono::steady_clock::now();
    if (now - pendingAttackAt > std::chrono::milliseconds(200)) {
        pendingAttackRID = 0;
        return;
    }

    float simMs = effectiveSimMs();
    if (simMs <= 0.f) {
        pendingAttackRID = 0;
        return;
    }

    sendLatencyProbe(simMs);
    pendingAttackRID = 0;
}

bool AfterTrack::isAttackPacket(SDK::Packet* packet, uint64_t& outTarget) const {
    auto id = packet->getID();
    auto base = reinterpret_cast<uintptr_t>(packet);

    if (id == SDK::PacketID::INTERACT) {
        if (*reinterpret_cast<uint8_t*>(base + 0x30) != 2) return false;
        outTarget = *reinterpret_cast<uint64_t*>(base + 0x38);
        return true;
    }

    if (id == SDK::PacketID::PLAYER_AUTH_INPUT) {
        auto txn = *reinterpret_cast<uintptr_t*>(base + 0xB0);
        if (!txn) return false;
        if (*reinterpret_cast<uint32_t*>(txn + 0x70) != 1) return false;
        outTarget = *reinterpret_cast<uint64_t*>(txn + 0x68);
        return true;
    }

    return false;
}

void AfterTrack::sendLatencyProbe(float offsetMs) {
    (void)offsetMs;
    if (!Signatures::MinecraftPackets_createPacket.result) return;

    auto ci = SDK::ClientInstance::get();
    if (!ci) return;
    auto* lp = ci->getLocalPlayer();
    if (!lp || !lp->packetSender) return;

    auto pkt = SDK::MinecraftPackets::createPacket(SDK::PacketID::NETWORK_STACK_LATENCY);
    if (!pkt) return;

    auto base = reinterpret_cast<uintptr_t>(pkt.get());
    uint64_t nowUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
    *reinterpret_cast<uint64_t*>(base + 0x30) = nowUs;
    *reinterpret_cast<uint8_t*>(base + 0x38) = 1;

    probeInFlight = true;
    lp->packetSender->sendToServer(pkt.get());
    probeInFlight = false;
}

void AfterTrack::onRenderLevel(RenderLevelEvent&) {
    if (AntiObs::isActive()) return;
    if (!std::get<BoolValue>(hitbox)) return;

    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->levelRenderer || !SDK::ScreenContext::instance3d) return;
    auto lp = ci->getLocalPlayer();
    if (!lp) return;

    if (predictions.empty()) return;

    auto material = SDK::MaterialPtr::getSelectionOverlayMaterial();
    MCDrawUtil3D dc(ci->levelRenderer, SDK::ScreenContext::instance3d, material);

    auto baseCol = std::get<ColorValue>(hitboxColor).getMainColor();
    int style = hitboxStyle.getSelectedKey();
    float thickness = std::get<FloatValue>(hitboxThickness).value / 10.f;
    std::vector<MCDrawUtil3D::ColoredBox> fills;
    std::vector<MCDrawUtil3D::ColoredThickBox> outlines;

    auto snap = EntityCache::get().snapshot();
    for (auto* actor : snap->actors) {
        if (!actor || actor == lp || !actor->isPlayer()) continue;
        if (actor->isInvisible()) continue;

        uint64_t rid = actor->getRuntimeID();
        AABB box;
        if (!getPredictedBox(rid, box)) continue;

        d2d::Color col(baseCol);
        if (style != style_outline) fills.push_back({ box, col });
        if (style != style_filled) outlines.push_back({ box, thickness, col });
    }

    dc.fillBoxes(fills);
    dc.drawThickBoxes(outlines);
}
