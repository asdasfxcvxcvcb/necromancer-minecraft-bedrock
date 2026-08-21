#pragma once
#include "client/feature/module/Module.h"

#include <chrono>
#include <random>

class AutoClicker : public Module {
public:
    AutoClicker();
    ~AutoClicker() = default;

    void onUpdate(Event& evG);
    void onDisable() override;
    void afterLoadConfig() override;

private:
    ValueType leftClick = BoolValue(true);
    ValueType blockBreak = BoolValue(true);
    ValueType prioritizeAttack = BoolValue(false);
    ValueType cpsLeft = FloatValue(10.f);
    ValueType cpsLeftMin = FloatValue(7.f);
    ValueType cpsLeftMax = FloatValue(13.f);
    ValueType rightClick = BoolValue(true);
    ValueType cpsRight = FloatValue(10.f);
    ValueType cpsRightMin = FloatValue(7.f);
    ValueType cpsRightMax = FloatValue(13.f);
    ValueType randomize = BoolValue(false);
    ValueType rightBlocksOnly = BoolValue(false);

    std::chrono::steady_clock::time_point nextLeftClick = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point nextRightClick = std::chrono::steady_clock::now();
    std::mt19937 rng;
    bool leftBlockHeldByUs = false;

    void pushAction(int button, bool down);
    void handleButton(int button, bool btnEnabled, float cpsFixed, float cpsMin, float cpsMax,
                      std::chrono::steady_clock::time_point& nextClick, std::chrono::steady_clock::time_point now);
    float sampleCps(float fixed, float minVal, float maxVal);
    bool isHoldingBlock();
    bool isAimingAtBlock();
    bool hasAttackableTarget();
    bool isPhysicallyHeld(int button);
};
