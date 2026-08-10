#include "pch.h"
#include "ForwardTrack.h"
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

ForwardTrack::ForwardTrack()
    : Module("ForwardTrack", LocalizeString::get("client.module.forwardTrack.name"),
             LocalizeString::get("client.module.forwardTrack.desc"), GAME, nokeybind) {
    auto simSet = addSliderSetting("maxSim", LocalizeString::get("client.module.forwardTrack.maxSim.name"),
                     LocalizeString::get("client.module.forwardTrack.maxSim.desc"), maxSim, FloatValue(0.f),
                     FloatValue(300.f), FloatValue(1.f));
    simSet->floatEditMax = static_cast<float>(maxSimCeilingMs);

    addSetting("useStackLatency", LocalizeString::get("client.module.forwardTrack.useStackLatency.name"),
               LocalizeString::get("client.module.forwardTrack.useStackLatency.desc"), useStackLatency);
    addSetting("hitbox", LocalizeString::get("client.module.forwardTrack.hitbox.name"),
               LocalizeString::get("client.module.forwardTrack.hitbox.desc"), hitbox);
    addSetting("hitboxColor", LocalizeString::get("client.module.forwardTrack.hitboxColor.name"),
               LocalizeString::get("client.module.forwardTrack.hitboxColor.desc"), hitboxColor, "hitbox"_istrue);
    hitboxStyle.addEntry(EnumEntry(style_outline, LocalizeString::get("client.module.forwardTrack.hitboxStyle.outline.name"),
                                   LocalizeString::get("client.module.forwardTrack.hitboxStyle.outline.desc")));
    hitboxStyle.addEntry(EnumEntry(style_filled, LocalizeString::get("client.module.forwardTrack.hitboxStyle.filled.name"),
                                   LocalizeString::get("client.module.forwardTrack.hitboxStyle.filled.desc")));
    hitboxStyle.addEntry(EnumEntry(style_both, LocalizeString::get("client.module.forwardTrack.hitboxStyle.both.name"),
                                   LocalizeString::get("client.module.forwardTrack.hitboxStyle.both.desc")));
    addEnumSetting("hitboxStyle", LocalizeString::get("client.module.forwardTrack.hitboxStyle.name"),
                   LocalizeString::get("client.module.forwardTrack.hitboxStyle.desc"), hitboxStyle, "hitbox"_istrue);

    this->listen<UpdateEvent>(&ForwardTrack::onUpdate);
    this->listen<AveragePingEvent>(&ForwardTrack::onAvgPing, true);
    this->listen<AttackEvent>(&ForwardTrack::onAttack);
    this->listen<SendPacketEvent>(&ForwardTrack::onSendPacket);
    this->listen<LeaveGameEvent>(&ForwardTrack::onLeaveGame);
    Eventing::get().listen<RenderLevelEvent, &ForwardTrack::onRenderLevel>(this);
}

void ForwardTrack::clearState() {
    velHistory.clear();
    predictions.clear();
    predValid.clear();
    lastPingMs = 0;
    pingKnown = false;
    pendingAttackRID = 0;
    probeInFlight = false;
}

void ForwardTrack::onEnable() {
    clearState();
}

void ForwardTrack::onDisable() {
    clearState();
}

void ForwardTrack::onLeaveGame(Event&) {
    clearState();
}

void ForwardTrack::onAvgPing(Event& evG) {
    auto& ev = reinterpret_cast<AveragePingEvent&>(evG);
    lastPingMs = ev.getPing();
    pingKnown = true;
}

float ForwardTrack::effectiveSimMs() const {
    if (!pingKnown || lastPingMs <= 0) return 0.f;
    float cap = std::clamp(std::get<FloatValue>(maxSim).value, 0.f, static_cast<float>(maxSimCeilingMs));
    return std::clamp(static_cast<float>(lastPingMs), 0.f, cap);
}

Vec3 ForwardTrack::lookDirFromRot(Vec2 rot) const {
    float yaw = (rot.y + 90.f) * (pi_f / 180.f);
    float pitch = rot.x * -(pi_f / 180.f);
    return { cosf(yaw) * cosf(pitch), 0.f, sinf(yaw) * cosf(pitch) };
}

Vec3 ForwardTrack::blendedVelocity(uint64_t rid, Vec3 liveVel) const {
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

bool ForwardTrack::samplePredictions() {
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
    std::unordered_map<uint64_t, std::deque<VelRecord>> newHistory;
    std::unordered_map<uint64_t, AABB> newPred;
    std::unordered_map<uint64_t, bool> newValid;

    auto snap = EntityCache::get().snapshot();
    for (auto* actor : snap->actors) {
        if (!actor || actor == lp || !actor->isPlayer()) continue;
        auto hp = actor->getHealth();
        if (!hp || *hp <= 0.f) continue;

        uint64_t rid = actor->getRuntimeID();

        Vec3 liveVel = actor->getPos() - actor->getPosOld();
        auto& hist = newHistory[rid];
        auto oldIt = velHistory.find(rid);
        if (oldIt != velHistory.end()) hist = oldIt->second;
        if (hist.empty() || now - hist.back().at >= std::chrono::milliseconds(20)) {
            hist.push_back({ now, liveVel });
            while (hist.size() > velHistoryLen) hist.pop_front();
        }

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
            newPred[rid] = res.finalBox;
            newValid[rid] = true;
        }
    }

    velHistory = std::move(newHistory);
    predictions = std::move(newPred);
    predValid = std::move(newValid);
    return true;
}

bool ForwardTrack::getPredictedBox(uint64_t runtimeID, AABB& out) {
    if (!isEnabled()) return false;
    auto it = predictions.find(runtimeID);
    if (it == predictions.end()) return false;
    auto vit = predValid.find(runtimeID);
    if (vit == predValid.end() || !vit->second) return false;
    out = it->second;
    return true;
}

void ForwardTrack::onUpdate(Event&) {
    samplePredictions();
}

void ForwardTrack::onAttack(Event& evG) {
    auto& ev = reinterpret_cast<AttackEvent&>(evG);
    auto* target = ev.getActor();
    if (!target) return;

    float simMs = effectiveSimMs();
    if (simMs <= 0.f) return;
    if (!std::get<BoolValue>(useStackLatency)) return;

    pendingAttackRID = target->getRuntimeID();
    pendingAttackAt = std::chrono::steady_clock::now();
}

void ForwardTrack::onSendPacket(Event& evG) {
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

bool ForwardTrack::isAttackPacket(SDK::Packet* packet, uint64_t& outTarget) const {
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

void ForwardTrack::sendLatencyProbe(float offsetMs) {
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

void ForwardTrack::onRenderLevel(RenderLevelEvent&) {
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

    auto snap = EntityCache::get().snapshot();
    for (auto* actor : snap->actors) {
        if (!actor || actor == lp || !actor->isPlayer()) continue;
        if (actor->isInvisible()) continue;

        uint64_t rid = actor->getRuntimeID();
        AABB box;
        if (!getPredictedBox(rid, box)) continue;

        d2d::Color col(baseCol);

        if (style != style_outline) {
            Vec3 lo = box.lower;
            Vec3 hi = box.higher;
            dc.fillQuad({ lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { hi.x, lo.y, hi.z }, { lo.x, lo.y, hi.z }, col);
            dc.fillQuad({ lo.x, hi.y, lo.z }, { hi.x, hi.y, lo.z }, { hi.x, hi.y, hi.z }, { lo.x, hi.y, hi.z }, col);
            dc.fillQuad({ lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { hi.x, hi.y, lo.z }, { lo.x, hi.y, lo.z }, col);
            dc.fillQuad({ lo.x, lo.y, hi.z }, { hi.x, lo.y, hi.z }, { hi.x, hi.y, hi.z }, { lo.x, hi.y, hi.z }, col);
            dc.fillQuad({ lo.x, lo.y, lo.z }, { lo.x, lo.y, hi.z }, { lo.x, hi.y, hi.z }, { lo.x, hi.y, lo.z }, col);
            dc.fillQuad({ hi.x, lo.y, lo.z }, { hi.x, lo.y, hi.z }, { hi.x, hi.y, hi.z }, { hi.x, hi.y, lo.z }, col);
        }
        if (style != style_filled) {
            d2d::Color lineCol = col;
            lineCol.a = std::max(lineCol.a, 0.9f);
            dc.drawBox(box, lineCol);
        }
        dc.flush();
    }
}
