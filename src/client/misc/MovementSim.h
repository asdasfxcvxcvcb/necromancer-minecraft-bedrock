#pragma once
#include "util/LMath.h"
#include <optional>

namespace SDK {
    class Actor;
    class BlockSource;
}

namespace MovementSim {
    struct Result {
        BlockPos landingBlock;
        float faceY;
        int ticksToImpact;
        Vec3 impactPos;
        Vec2 faceMin;
        Vec2 faceMax;
        bool landsInLiquid;
    };

    bool isLiquidAt(SDK::BlockSource* region, BlockPos const& pos);

    std::optional<Result> predictLanding(SDK::Actor* actor, int maxTicks = 200);
}
