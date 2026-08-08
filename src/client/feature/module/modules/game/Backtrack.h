#pragma once
#include "../../Module.h"
#include <chrono>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SDK {
    class Actor;
    class Packet;
}

class RenderLevelEvent;

class Backtrack : public Module {
public:
    Backtrack();

    void onEnable() override;
    void onDisable() override;
    void onUpdate(Event& evG);
    void onAfterMove(Event& evG);
    void onSendPacket(Event& evG);
    void onReceivePacket(Event& evG);
    void onAttack(Event& evG);
    void onClick(Event& evG);
    void onLeaveGame(Event& evG);
    void onRenderLevel(RenderLevelEvent& event);

    // Ghost box for a player at the currently configured age, in world space. Lets
    // Aimbot and Triggerbot aim at the lag record instead of the live model without
    // duplicating the sampling or the interpolation.
    bool getGhostBox(uint64_t runtimeID, AABB& out);

private:
    struct Sample {
        std::chrono::steady_clock::time_point at;
        Vec3 pos;
        // The enemy's real hitbox at this instant, captured rather than rebuilt
        // later, so the drawn box matches the box that actually existed here.
        AABB box;
        // Whether they were off the ground when this was recorded. Captured now because
        // a record from 500ms ago has no live ground state left to query. Debounced: a
        // single stray tick does not count, only a sustained one.
        bool airborne;
        // Undebounced reading, kept so the next sample can see whether this one was
        // already off the ground.
        bool rawAirborne;
    };

    struct QueuedAttack {
        uint64_t runtimeID;
        std::chrono::steady_clock::time_point fireAt;
        Vec3 ghostPos;
        AABB ghostBox;
        // Where the crosshair ray actually crossed the ghost box. Reporting the box
        // centre instead made every hit on a given ghost claim the same point, so the
        // half you aimed at changed nothing the server could see.
        Vec3 hitPoint;
        // Latency to announce for this specific hit. Carried per-attack rather than in
        // a shared field so a second click cannot overwrite the first one's value
        // before it is used.
        float reportedOffset;
    };

    // Samples land every 20ms, so a request can legitimately be off by up to one
    // interval. Beyond this we don't have history at the requested age.
    static constexpr float sampleToleranceMs = 60.f;
    // Hard ceiling for any age value. Sliders only reach 2000 in the UI, but a
    // manually typed config value is allowed up to here, and buffer retention is
    // derived from this so the two can never drift apart.
    static constexpr int maxRecordMs = 50000;
    static constexpr size_t maxSamples = 3200;
    static constexpr int maxLatencyMs = 50000;
    // Vanilla survival melee reach. The ghost-box raycast must not exceed this and
    // must not pad the box, or Backtrack silently becomes a reach hack: it fires
    // GameMode::attack at whatever the ray found, so the search radius IS the reach.
    static constexpr float legitReach = 3.f;
    // How long a per-hit timestamp override stays armed. Long enough to outlive the
    // gap between periodic NetworkStackLatency echoes, short enough that it does not
    // linger over an unrelated later hit.
    static constexpr int reportedOffsetHoldMs = 1000;

    void clearState();
    void samplePositions();
    void processAirClick();
    void processAttackQueue();
    SDK::Actor* resolveActor(uint64_t runtimeID);
    bool cameraRay(Vec3& outEye, Vec3& outDir);
    SDK::Actor* pickStaleTarget(float maxDist, Vec3* outGhostPos = nullptr, AABB* outGhostBox = nullptr,
                                Vec3* outHitPoint = nullptr);
    bool isAttackPacket(SDK::Packet* packet, uint64_t& outTarget);
    const Sample* findSample(uint64_t runtimeID, float ageMs, bool& fresh);
    // Reconstructs the position between two recorded samples. Records land every
    // 20ms, so an age that falls between them has no stored sample; without this
    // the box snaps to the nearest one and the attack is built from a position the
    // slider never asked for. Only used when Only Last Record is off.
    bool interpolateSample(uint64_t runtimeID, float ageMs, Sample& out);
    // Ages of every ghost we currently draw, oldest last. Shared by the renderer and
    // the target picker so what is drawn is exactly what can be hit.
    void buildGhostAges(std::vector<float>& out, uint64_t runtimeID);
    // Ghosts sit at ages the sliders were never set to, and the server rewinds using
    // the age our timestamp implies -- so hitting one means moving the offset to that
    // ghost's age for the attack. Returns the value to report, or -1 to leave alone.
    float adjustedOffsetFor(float ghostAge);
    // Sends a NetworkStackLatency with a backdated timestamp. The client only echoes
    // when the server asks, so we cannot rely on one happening near our attack -- we
    // originate it ourselves right before the hit.
    void sendLatencyProbe(float offsetMs);

    void applyFakeLatency();
    // How far back the ghost box / target lookup should reach. Fake latency counts
    // toward this: if the server believes we are 200ms behind, the position it
    // validates our hits against is 200ms further into the past, so the box has to
    // move with it instead of making the user hand-match two sliders.
    float ghostAgeMs();
    float ghostAgeMsFor(uint64_t runtimeID);

    EnumData hitboxStyle;
    static constexpr int style_outline = 0;
    static constexpr int style_filled = 1;
    static constexpr int style_both = 2;

    ValueType timeMs = FloatValue(150.f);
    ValueType fakeLatencyMs = FloatValue(0.f);
    ValueType stackLatencyDelayMs = FloatValue(0.f);
    ValueType onlyLastRecord = BoolValue(false);
    // Records taken while the enemy was off the ground stay visible but cannot be
    // attacked by anything.
    ValueType invalidateAirborne = BoolValue(false);
    // Pins every ghost to the position it held the moment the freeze began, instead
    // of letting it slide forward with the enemy.
    ValueType freezeBacktrack = BoolValue(false);
    // How many ghosts to spread across the window. 1 reproduces the old single box.
    ValueType ghostCount = FloatValue(4.f);
    ValueType hitbox = BoolValue(true);
    ValueType hitboxColor = ColorValue(1.f, 0.55f, 0.f, 0.6f);
    ValueType throughWalls = BoolValue(true);

    std::unordered_map<uint64_t, std::deque<Sample>> buffers;
    // One server tick. Records are spaced on tick boundaries so a shift lands exactly on
    // a neighbouring one.
    static constexpr float recordStepMs = 50.f;
    std::unordered_set<uint64_t> seenScratch;
    std::deque<QueuedAttack> attackQueue;
    // Last value pushed into LatencySpoof, so the slider is only applied on change.
    uint32_t appliedLatencyMs = 0;
    uint64_t pendingRID = 0;
    std::chrono::steady_clock::time_point pendingAt {};
    uint64_t swallowRID = 0;
    std::chrono::steady_clock::time_point swallowUntil {};
    uint64_t reissueRID = 0;
    std::chrono::steady_clock::time_point reissueUntil {};
    bool hasClickPending = false;
    std::chrono::steady_clock::time_point clickPendingAt {};
    std::chrono::steady_clock::time_point lastAttackEventAt {};
    // Instant the freeze began. While set, ghost ages are measured from here rather
    // than from now, which is what pins them in place.
    std::chrono::steady_clock::time_point freezeAt {};
    bool freezeActive = false;
    // Age of the ghost the crosshair last landed on, so the attack can report the
    // matching timestamp offset instead of the slider's.
    float aimedGhostAge = -1.f;
    // Offset the next NetworkStackLatency echo should report, so the server's rewind
    // lands on the ghost we aimed at. -1 means "use the slider".
    float pendingReportedOffset = -1.f;
    // Set while our own probe is being sent, so the send hook does not treat it as
    // game traffic and rewrite it a second time.
    bool probeInFlight = false;
    // Echoes are periodic and not synced to our attacks, so the override has to stay
    // live long enough for at least one to carry it.
    std::chrono::steady_clock::time_point reportedOffsetUntil {};
    std::vector<float> ghostAgeScratch;
    // Pending damage confirmations: after firing we watch the target's health for a
    // short window so the log can say whether the hit actually registered, instead of
    // only that we sent it.
    struct PendingConfirm {
        uint64_t runtimeID;
        float hpAtFire;
        float ghostAge;
        float reportedOffset;
        // Distance from the click point to the top of the ghost box. This is the value
        // that actually predicts whether damage lands, so it is what the trace reports.
        float belowTop;
        std::chrono::steady_clock::time_point firedAt;
        bool done;
    };
    std::deque<PendingConfirm> confirmQueue;
    void processConfirmations();
    static constexpr int confirmWindowMs = 700;
};
