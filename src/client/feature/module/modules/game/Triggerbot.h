#pragma once
#include "client/feature/module/Module.h"

#include <chrono>

class Triggerbot : public Module {
public:
    Triggerbot();

    void onUpdate(Event& evG);
    void onAfterMove(Event& evG);
    void onDisable() override;
    void afterLoadConfig() override;

private:
    ValueType players = BoolValue(true);
    ValueType mobs = BoolValue(false);
    ValueType ignoreFriends = BoolValue(true);
    ValueType hitBehindWall = BoolValue(false);

    ValueType cps = FloatValue(12.f);
    ValueType range = FloatValue(3.f);

    ValueType criticalOnly = BoolValue(false);
    ValueType maceSmashOnly = BoolValue(false);
    ValueType skipCritsIfKillable = BoolValue(true);
    // Fire at the backtrack ghost box instead of the live model.
    ValueType backtrackTarget = BoolValue(false);
    // Refuse a second hit at the same height on one target: some servers drop a repeat
    // at an already-used vertical band, so the crosshair has to move first.
    ValueType noRepeatPoint = BoolValue(false);
    ValueType pointGap = FloatValue(0.3f);

    enum class TargetRecord {
        Live,
        Backtrack,
        AfterTrack,
    };

    struct TargetSelection {
        SDK::Actor* actor = nullptr;
        TargetRecord record = TargetRecord::Live;
        AABB box {};
        Vec3 hitPoint {};
        float recordAgeMs = -1.f;
        bool obstructed = false;
    };

    struct PendingAttack {
        uint64_t runtimeID = 0;
        TargetRecord record = TargetRecord::Live;
        AABB box {};
        Vec3 hitPoint {};
        float recordAgeMs = -1.f;
        bool obstructed = false;
        std::chrono::steady_clock::time_point fireAt {};
        bool active = false;
    };

    std::chrono::steady_clock::time_point nextAttack = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastJumpTime = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    bool wasOnGround = true;
    // Height of the last accepted hit, for the no-repeat rule.
    uint64_t lastHitTarget = 0;
    float lastHitY = 0.f;
    bool hasLastHit = false;
    PendingAttack pendingAttack {};
    class Backtrack* backtrackModule = nullptr;
    bool backtrackResolved = false;
    class AfterTrack* afterTrackModule = nullptr;
    bool afterTrackResolved = false;

    TargetSelection pickTarget(float maxRange);
    bool canFire(SDK::Actor* target);
    bool normalHitKills(SDK::Actor* target);
    // Height at which the crosshair currently crosses the target. Uses the ghost box
    // when backtrack targeting is on, since that is the box being hit.
    bool currentAimHeight(TargetSelection const& target, float& outY);
    bool performDirectAttack(SDK::LocalPlayer* lp, TargetSelection const& target);
    Backtrack* resolveBacktrack();
    AfterTrack* resolveAfterTrack();
};
