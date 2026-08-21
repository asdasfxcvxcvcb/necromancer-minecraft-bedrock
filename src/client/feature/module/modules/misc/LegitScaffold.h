#pragma once
#include "client/feature/module/Module.h"
#include "util/LMath.h"
#include <chrono>
#include <vector>

namespace SDK {
    class BlockSource;
    class Player;
}

class LegitScaffold : public Module {
public:
    LegitScaffold();

    void onBuildBlock(Event& evG);
    void onTick(Event& evG);
    void onDisable() override;

    [[nodiscard]] int blocksPerPlacement() const;

private:
    static constexpr uint8_t FACE_DOWN = 0;
    static constexpr uint8_t FACE_UP = 1;
    static constexpr uint8_t FACE_NORTH = 2;
    static constexpr uint8_t FACE_SOUTH = 3;
    static constexpr uint8_t FACE_WEST = 4;
    static constexpr uint8_t FACE_EAST = 5;
    static constexpr uint8_t FACE_NONE = 0xFF;

    ValueType allowUp = BoolValue(true);
    ValueType speedBridge = BoolValue(true);
    ValueType bridgeReach = FloatValue(3.f);
    ValueType straightOnly = BoolValue(false);
    ValueType bridgeDrop = FloatValue(2.f);
    ValueType bridgeDelay = FloatValue(55.f);
    ValueType bridgeBlocksPerTick = FloatValue(2.f);
    ValueType requireLookDown = BoolValue(true);
    ValueType minPitch = FloatValue(5.f);
    ValueType requireHoldUse = BoolValue(true);
    ValueType requireMoving = BoolValue(false);

    ValueType directionBased = BoolValue(false);
    ValueType dirForward = BoolValue(true);
    ValueType dirBackward = BoolValue(true);
    ValueType dirLeft = BoolValue(true);
    ValueType dirRight = BoolValue(true);
    ValueType dirSimTicks = FloatValue(3.f);
    ValueType dirBlendCamera = FloatValue(0.f);
    ValueType dirDiagonalFill = BoolValue(true);
    ValueType dirMinSpeed = FloatValue(0.02f);

    struct Candidate {
        BlockPos support {};
        uint8_t face = FACE_NONE;
    };

    bool bridging = false;
    int bridgeY = 0;
    bool hasBridgeY = false;
    std::chrono::steady_clock::time_point nextPlace {};

    bool shouldAllow(BlockPos const& blockPos, uint8_t face) const;
    bool isBelowFeet(BlockPos const& blockPos) const;

    [[nodiscard]] bool findSupport(SDK::BlockSource* region, BlockPos const& target, BlockPos& outSupport,
                                   uint8_t& outFace) const;
    void collectCandidates(SDK::Player* plr, SDK::BlockSource* region, int placeY,
                           std::vector<Candidate>& out) const;
    void collectUnderCandidate(SDK::Player* plr, SDK::BlockSource* region, int placeY,
                               std::vector<Candidate>& out) const;
    [[nodiscard]] Vec3 resolveBridgeDir(SDK::Player* plr) const;
};
