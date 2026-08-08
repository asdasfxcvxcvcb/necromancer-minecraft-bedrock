#pragma once
#include "client/feature/module/Module.h"
#include "util/LMath.h"

class LegitScaffold : public Module {
public:
    LegitScaffold();

    void onBuildBlock(Event& evG);

private:
    static constexpr uint8_t FACE_DOWN = 0;
    static constexpr uint8_t FACE_UP = 1;
    static constexpr uint8_t FACE_NORTH = 2;
    static constexpr uint8_t FACE_SOUTH = 3;
    static constexpr uint8_t FACE_WEST = 4;
    static constexpr uint8_t FACE_EAST = 5;

    ValueType allowUp = BoolValue(true);

    bool shouldAllow(BlockPos const& blockPos, uint8_t face) const;
    bool isBelowFeet(BlockPos const& blockPos) const;
};
