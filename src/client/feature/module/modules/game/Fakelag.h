#pragma once
#include "../../Module.h"
#include <chrono>
#include <random>

namespace SDK {
    class LocalPlayer;
}

class RenderLevelEvent;
class RenderLayerEvent;

class Fakelag : public Module {
public:
    Fakelag();

    void onEnable() override;
    void onDisable() override;
    void onUpdate(Event& evG);
    void onSendPacket(Event& evG);
    void onPacketReceive(Event& evG);
    void onAttack(Event& evG);
    void onClick(Event& evG);
    void onKey(Event& evG);
    void onLeaveGame(Event& evG);
    void onRenderLevel(RenderLevelEvent& event);
    void onRenderLayer(RenderLayerEvent& event);

private:
    void flushKnockback();
    void requestRelease();
    bool enemyWatchingGhost(SDK::LocalPlayer* lp);
    bool isPlayerRuntimeID(uint64_t runtimeID);
    void rollRangeThreshold();
    void captureChokeState(SDK::LocalPlayer* lp);
    bool damageOnlyMode();
    bool wallBetweenGhostAndPlayer(SDK::LocalPlayer* lp);
    bool isBreakingBlock(SDK::LocalPlayer* lp);
    bool isLootContainer(BlockPos const& pos);
    bool releaseForInteractionActive(std::chrono::steady_clock::time_point now) const;

    EnumData chokedHitboxStyle;
    static constexpr int style_outline = 0;
    static constexpr int style_filled = 1;
    static constexpr int style_both = 2;

    ValueType suppressKnockback = BoolValue(true);
    ValueType expertSettings = BoolValue(false);
    ValueType adaptive = BoolValue(true);
    ValueType delayBetweenChokes = FloatValue(0.5f);
    ValueType maxTime = FloatValue(2.f);
    ValueType minRange = FloatValue(1.f);
    ValueType maxRange = FloatValue(6.f);
    ValueType randomizeRange = BoolValue(false);
    ValueType unchokeOnHit = BoolValue(true);
    ValueType unchokeOnBuild = BoolValue(false);
    ValueType unchokeOnBreak = BoolValue(false);
    ValueType unchokeOnUseItem = BoolValue(false);
    ValueType unchokeWhileLooting = BoolValue(false);
    ValueType unchokeWhenOpeningInventory = BoolValue(false);
    ValueType riskyHeightChange = BoolValue(true);
    ValueType unchokeAtCorners = BoolValue(true);
    ValueType cornerThickness = FloatValue(0.7f);
    ValueType chokeWhenDamaged = FloatValue(0.f);
    ValueType onlyEnemyHit = BoolValue(true);
    ValueType showChokedHitbox = BoolValue(true);
    ValueType chokedHitboxColor = ColorValue(0.f, 0.f, 1.f, 0.5f);
    ValueType chokedHitboxThickness = FloatValue(0.3f);

    Vec3 heldKnockback {};
    bool hasHeldKnockback = false;

    Vec3 chokeOrigin {};
    Vec3 chokeBoxLower {};
    Vec3 chokeBoxHigher {};
    std::chrono::steady_clock::time_point chokeStart {};
    std::chrono::steady_clock::time_point lastHitAt {};
    std::chrono::steady_clock::time_point lastEnemySwingAt {};
    std::chrono::steady_clock::time_point gapUntil {};
    std::chrono::steady_clock::time_point inventoryAttemptUntil {};
    std::chrono::steady_clock::time_point lootAttemptUntil {};
    std::chrono::steady_clock::time_point inventoryScreenSeenAt {};
    std::chrono::steady_clock::time_point lootScreenSeenAt {};
    bool hitPending = false;
    bool releasePending = false;
    bool gapPhase = false;
    bool damageChokeActive = false;
    bool wasDamageOnly = false;
    std::chrono::steady_clock::time_point kbBankedAt {};

    float rangeThreshold = 6.f;
    float breakProgressSeen = 0.f;
    std::mt19937 rng { std::random_device {}() };
};
