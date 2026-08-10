#pragma once
#include "../../Module.h"
#include <chrono>
#include <deque>
#include <unordered_map>

namespace SDK {
    class Actor;
    class Packet;
}

class RenderLevelEvent;
class SendPacketEvent;

class ForwardTrack : public Module {
public:
    ForwardTrack();

    void onEnable() override;
    void onDisable() override;
    void onUpdate(Event& evG);
    void onAvgPing(Event& evG);
    void onAttack(Event& evG);
    void onSendPacket(Event& evG);
    void onLeaveGame(Event& evG);
    void onRenderLevel(RenderLevelEvent& event);

private:
    struct VelRecord {
        std::chrono::steady_clock::time_point at;
        Vec3 vel;
    };

    static constexpr float tickStepMs = 50.f;
    static constexpr float fuzzyWeight = 0.15f;
    static constexpr size_t velHistoryLen = 5;
    static constexpr int maxSimCeilingMs = 5000;

    void clearState();
    Vec3 lookDirFromRot(Vec2 rot) const;
    Vec3 blendedVelocity(uint64_t rid, Vec3 liveVel) const;
    float effectiveSimMs() const;
    bool samplePredictions();
    bool getPredictedBox(uint64_t runtimeID, AABB& out);
    void sendLatencyProbe(float offsetMs);
    bool isAttackPacket(SDK::Packet* packet, uint64_t& outTarget) const;

    EnumData hitboxStyle;
    static constexpr int style_outline = 0;
    static constexpr int style_filled = 1;
    static constexpr int style_both = 2;

    ValueType maxSim = FloatValue(150.f);
    ValueType useStackLatency = BoolValue(false);
    ValueType hitbox = BoolValue(true);
    ValueType hitboxColor = ColorValue(0.f, 1.f, 0.7f, 0.6f);

    int lastPingMs = 0;
    bool pingKnown = false;

    std::unordered_map<uint64_t, std::deque<VelRecord>> velHistory;
    std::unordered_map<uint64_t, AABB> predictions;
    std::unordered_map<uint64_t, bool> predValid;

    uint64_t pendingAttackRID = 0;
    std::chrono::steady_clock::time_point pendingAttackAt {};
    bool probeInFlight = false;
};
