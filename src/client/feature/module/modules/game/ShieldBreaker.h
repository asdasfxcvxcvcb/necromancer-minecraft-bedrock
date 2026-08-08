#pragma once
#include "client/feature/module/Module.h"

#include <chrono>

namespace SDK {
    class Actor;
    class Player;
    class LocalPlayer;
    class HitResult;
}

class ShieldBreaker : public Module {
public:
    ShieldBreaker();

    void onUpdate(Event& evG);
    void onAfterMove(Event& evG);
    void onDisable() override;

private:
    ValueType useDefaultRange = BoolValue(true);
    ValueType range = FloatValue(4.5f);
    ValueType ignoreFriends = BoolValue(true);
    ValueType requireBlocking = BoolValue(true);
    ValueType wallCheck = BoolValue(true);
    ValueType cooldown = FloatValue(5.f);
    ValueType switchDelay = FloatValue(0.f);

    enum class Phase {
        Idle,
        HoldingAxe,
        ReadyToHit,
    };

    std::chrono::steady_clock::time_point nextAttack = std::chrono::steady_clock::now();
    uint64_t lastBrokenTarget = 0;
    std::chrono::steady_clock::time_point lastBreakTime {};

    Phase phase = Phase::Idle;
    int savedSlot = -1;
    int pendingAxeSlot = -1;
    uint64_t pendingTargetId = 0;
    std::chrono::steady_clock::time_point switchedAt {};

    SDK::Actor* pickTarget(SDK::LocalPlayer* lp, float maxRange);
    bool targetHasShield(SDK::Actor* target);
    bool targetIsBlocking(SDK::Actor* target);
    int findAxeSlot(SDK::Player* lp);
    bool performAttack(SDK::LocalPlayer* lp, SDK::Actor* target);
    void finishPendingSwap(SDK::Player* lp);
    void clearPendingSwap();
};
