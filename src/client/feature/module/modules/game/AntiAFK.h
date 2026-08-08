#pragma once
#include "../../Module.h"
#include <atomic>
#include <chrono>
#include <optional>
#include <random>

namespace SDK {
    class BlockSource;
    class LocalPlayer;
    struct MoveInputComponent;
}

class AntiAFK : public Module {
public:
    AntiAFK();

    void onEnable() override;
    void onDisable() override;
    void onTick(Event& evG);
    void onAfterMove(Event& evG);
    void onUserInput(Event& evG);
    void onTurn(Event& evG);

private:
    enum class Phase { Waiting, WalkingOut, Dwell, Returning };

    using Clock = std::chrono::steady_clock;

    void markActivity();
    Clock::time_point lastActivity() const;
    void abortCycle(SDK::MoveInputComponent* input);
    void releaseOwnInput(SDK::MoveInputComponent* input);
    void turnBy(SDK::LocalPlayer* lp, float yawDelta);
    bool userIsMoving(SDK::MoveInputComponent* input) const;
    bool rayHitsSolid(SDK::BlockSource* region, Vec3 const& start, Vec3 const& end);
    bool groundBelow(SDK::BlockSource* region, Vec3 const& feetCenter);
    bool bodyClear(SDK::BlockSource* region, BlockPos const& bp);
    bool pathClear(SDK::BlockSource* region, Vec3 const& fromFeet, Vec3 const& toFeet);
    bool isWalkable(SDK::BlockSource* region, BlockPos const& bp);
    std::optional<Vec3> pickDestination(SDK::LocalPlayer* lp);
    bool driveToward(SDK::LocalPlayer* lp, Vec3 const& target);
    void scheduleNext();
    std::chrono::nanoseconds currentWaitDelay() const;

    ValueType interval = FloatValue(30.f);
    ValueType jitter = BoolValue(true);
    ValueType move = BoolValue(true);
    ValueType spin = BoolValue(true);
    ValueType jump = BoolValue(true);

    Phase phase = Phase::Waiting;
    float jitterMult = 1.f;
    std::chrono::nanoseconds currentDelay { 0 };
    Clock::time_point lastCycleEnd {};
    Clock::time_point phaseStart {};
    Clock::time_point cycleStart {};
    Clock::time_point dwellUntil {};
    Clock::time_point lastProgressAt {};
    float lastDist = 0.f;
    Vec3 walkTarget {};
    Vec3 homePos {};
    bool driving = false;
    int jumpTicks = 0;
    bool selfTurn = false;
    std::atomic<long long> lastActivityNs { 0 };
    std::mt19937 rng { std::random_device {}() };
};
