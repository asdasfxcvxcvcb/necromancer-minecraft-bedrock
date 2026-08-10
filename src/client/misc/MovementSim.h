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

    struct ForwardSample {
        Vec3 pos;
        AABB box;
        Vec3 velocity;
        bool onGround;
    };

    struct ForwardResult {
        bool valid = false;
        AABB finalBox;
        Vec3 finalPos;
        Vec3 finalVelocity;
        std::vector<ForwardSample> trace;
        bool hitWall = false;
        bool hitFloor = false;
    };

    bool isLiquidAt(SDK::BlockSource* region, BlockPos const& pos);

    std::optional<Result> predictLanding(SDK::Actor* actor, int maxTicks = 200);

    ForwardResult predictForward(SDK::Actor* actor, Vec3 seedVel, int ticks, bool recordTrace = false);
}
