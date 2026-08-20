#pragma once
#include "client/feature/module/Module.h"
#include <chrono>
#include <string>
#include <vector>

namespace SDK {
    class ContainerScreenController;
    class ItemStack;
}

class ChestStealer : public Module {
public:
    ChestStealer();

    void onRenderLayer(Event& evG);
    void onTick(Event& evG);
    void onEnable() override;
    void onDisable() override;

private:
    ValueType time = FloatValue(1.f);
    ValueType closeAfterLoot = BoolValue(false);
    ValueType enhanced = BoolValue(false);
    ValueType autoOrganize = BoolValue(false);

    SDK::ContainerScreenController* controller = nullptr;
    std::chrono::steady_clock::time_point lastSeen {};
    std::chrono::steady_clock::time_point lastAction {};
    std::chrono::steady_clock::time_point sessionStart {};
    bool containerScreen = false;

    enum class Phase { Scan,
                       Steal,
                       Organize,
                       Done };
    Phase phase = Phase::Scan;

    std::vector<int> plan;
    size_t planCursor = 0;
    int lootedStacks = 0;
    int verifySlot = -1;
    int verifyCount = 0;
    int verifyAttempts = 0;
    bool exitRequested = false;
    int stableScans = 0;
    int totalMoves = 0;

    void resetSession();
    SDK::ItemStack* readSlot(const std::string& collection, int slot);
    std::chrono::steady_clock::duration perActionDelay() const;
    bool processAction(std::chrono::steady_clock::time_point now);
    bool buildPlan(std::chrono::steady_clock::time_point now);
    bool processSteal();
    bool processOrganize();
    bool tryMove(const std::string& srcColl, int srcIdx, const std::string& dstColl, int dstIdx);
    void finish();
};
