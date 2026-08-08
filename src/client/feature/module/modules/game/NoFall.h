#pragma once
#include "client/feature/module/Module.h"
#include "util/LMath.h"
#include <chrono>
#include <optional>

namespace SDK {
    class Player;
    class LocalPlayer;
}

class NoFall : public Module {
public:
    NoFall();

    void onUpdate(Event& evG);
    void onTick(Event& evG);
    void runClutch();
    void onSendPacket(Event& evG);
    void onAfterMove(Event& evG);
    void onEnable() override;
    void onDisable() override;
    void afterLoadConfig() override;

private:
    EnumData mode;
    ValueType minDamage = FloatValue(4.f);
    ValueType aimDistance = FloatValue(40.f);
    ValueType preSwitchDistance = FloatValue(20.f);
    ValueType placeReach = FloatValue(4.5f);
    ValueType armTickLead = FloatValue(14.f);
    ValueType forceWhenLow = BoolValue(true);
    ValueType disableWithMace = BoolValue(true);
    ValueType ignorePlacedWater = BoolValue(true);
    ValueType pickUpWater = BoolValue(true);
    ValueType useFakelag = BoolValue(false);
    ValueType chokeTicks = FloatValue(10.f);
    ValueType freeEyeWhileFrozen = BoolValue(true);

    enum class ClutchState { Idle, Aiming, ChokePre, ChokeRelease, Placed, ChokePost, WaitLanding, PickingUp };

    ClutchState state = ClutchState::Idle;
    int originalSlot = -1;
    int clutchBucketSlot = -1;
    int placeAttempts = 0;
    bool preSwitched = false;
    bool chokeActive = false;
    bool allowOnePacket = false;
    bool freezeActive = false;
    bool sawFallingAfterPlace = false;
    Vec3 freezePos {};
    std::chrono::steady_clock::time_point unfrozeAt {};
    int chokedPackets = 0;
    Vec3 chokeAimPoint {};
    std::chrono::steady_clock::time_point chokeStartedAt {};
    BlockPos placedWaterPos {};
    BlockPos placeTargetBlock {};
    float fallDistance = 0.f;
    float lastY = 0.f;
    bool hasLastY = false;
    float cachedProtection = 1.f;
    std::chrono::steady_clock::time_point protectionCachedAt {};
    std::chrono::steady_clock::time_point lastAimFrame {};
    std::chrono::steady_clock::time_point placedAt {};
    std::chrono::steady_clock::time_point cooldownUntil {};
    std::chrono::steady_clock::time_point lastWindowLog {};
    std::chrono::steady_clock::time_point slowFallSince {};
    std::chrono::steady_clock::time_point lastBucketWarn {};
    std::chrono::steady_clock::time_point lastFallLog {};
    std::chrono::steady_clock::time_point landedAt {};
    std::chrono::steady_clock::time_point pickupStartedAt {};
    std::chrono::steady_clock::time_point pickupClickedAt {};
    std::chrono::steady_clock::time_point lastStateLog {};

    float getProtectionFactor(SDK::Player* lp, std::chrono::steady_clock::time_point now);
    void restoreSlot(SDK::Player* lp);
    void abortClutch(SDK::Player* lp, char const* reason);
    void resetClutch();
    bool maceLockout(SDK::Player* lp);
    float aimAtWater(SDK::LocalPlayer* lp);
    float aimAtPoint(SDK::LocalPlayer* lp, Vec3 const& point);
    bool runPickup(SDK::LocalPlayer* lp, std::chrono::steady_clock::time_point now);
    void startChoke();
    void stopChoke();
};
