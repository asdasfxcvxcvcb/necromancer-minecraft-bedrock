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
    std::chrono::steady_clock::time_point criticalWaitUntil {};
    bool criticalWaitActive = false;
    bool wasOnGround = true;
    PendingAttack pendingAttack {};
    class Backtrack* backtrackModule = nullptr;
    bool backtrackResolved = false;
    class AfterTrack* afterTrackModule = nullptr;
    bool afterTrackResolved = false;

    TargetSelection pickTarget(float maxRange);
    bool canFire(SDK::Actor* target);
    bool normalHitKills(SDK::Actor* target);
    bool performDirectAttack(SDK::LocalPlayer* lp, TargetSelection const& target);
    Backtrack* resolveBacktrack();
    AfterTrack* resolveAfterTrack();
};
