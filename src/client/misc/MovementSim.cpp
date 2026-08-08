#include "pch.h"
#include "MovementSim.h"
#include "BlockSolid.h"
#include "util/Logger.h"
#include <mc/common/world/actor/Actor.h>
#include <mc/common/world/level/Dimension.h>
#include <mc/common/world/level/BlockSource.h>
#include <mc/common/world/level/block/Block.h>
#include <mc/common/world/level/block/BlockLegacy.h>
#include <atomic>
#include <vector>
#include <cmath>

namespace {
    constexpr float gravity = 0.08f;
    constexpr float verticalDrag = 0.98f;
    constexpr float horizontalDrag = 0.91f;
    constexpr float terminalVelocity = -3.92f;
    constexpr float eps = 0.00001f;
    constexpr float inset = 0.001f;
    constexpr int worldFloorY = -64;
    constexpr size_t fetchAABBsVtIndex = 0x10;

    std::atomic<int> shapeMode { 0 };

    int floorI(float v) {
        return static_cast<int>(std::floor(v));
    }

    bool blockIsLiquid(SDK::BlockSource* region, int x, int y, int z) {
        return BlockSolid::isLiquidAt(region, x, y, z);
    }

    bool fetchShapesRaw(SDK::BlockSource* region, std::vector<AABB>* out, AABB const* query) {
        __try {
            using Fn = void(__fastcall*)(void*, std::vector<AABB>&, AABB const&, bool);
            Fn* vtable = *reinterpret_cast<Fn**>(region);
            vtable[fetchAABBsVtIndex](region, *out, *query, false);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool validateShapes(std::vector<AABB> const& shapes, AABB const& query) {
        if (shapes.size() > 512) return false;
        for (auto const& s : shapes) {
            if (!std::isfinite(s.lower.x) || !std::isfinite(s.lower.y) || !std::isfinite(s.lower.z) ||
                !std::isfinite(s.higher.x) || !std::isfinite(s.higher.y) || !std::isfinite(s.higher.z)) {
                return false;
            }
            if (s.higher.x < s.lower.x || s.higher.y < s.lower.y || s.higher.z < s.lower.z) return false;
            if (s.higher.x - s.lower.x > 64.f || s.higher.y - s.lower.y > 64.f || s.higher.z - s.lower.z > 64.f) {
                return false;
            }
            if (s.higher.x < query.lower.x - 4.f || s.lower.x > query.higher.x + 4.f ||
                s.higher.y < query.lower.y - 4.f || s.lower.y > query.higher.y + 4.f ||
                s.higher.z < query.lower.z - 4.f || s.lower.z > query.higher.z + 4.f) {
                return false;
            }
        }
        return true;
    }

    void gridGather(SDK::BlockSource* region, AABB const& query, std::vector<AABB>& out) {
        int x0 = floorI(query.lower.x);
        int x1 = floorI(query.higher.x);
        int y0 = std::max(worldFloorY, floorI(query.lower.y));
        int y1 = floorI(query.higher.y);
        int z0 = floorI(query.lower.z);
        int z1 = floorI(query.higher.z);
        if (static_cast<long long>(x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > 4096) return;
        for (int x = x0; x <= x1; x++) {
            for (int y = y0; y <= y1; y++) {
                for (int z = z0; z <= z1; z++) {
                    if (!BlockSolid::isCollidable(region, x, y, z)) continue;
                    out.push_back(AABB {
                        Vec3 { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) },
                        Vec3 { static_cast<float>(x) + 1.f, static_cast<float>(y) + 1.f, static_cast<float>(z) + 1.f },
                    });
                }
            }
        }
    }

    void gatherShapes(SDK::BlockSource* region, AABB const& query, std::vector<AABB>& out) {
        out.clear();
        int mode = shapeMode.load(std::memory_order_relaxed);
        if (mode == 2) {
            gridGather(region, query, out);
            return;
        }

        bool ok = fetchShapesRaw(region, &out, &query);
        if (!ok || !validateShapes(out, query)) {
            shapeMode.store(2, std::memory_order_relaxed);
            Logger::Warn("[MovementSim] fetchAABBs failed (ok={} count={}), using solid-block grid", ok, out.size());
            out.clear();
            gridGather(region, query, out);
            return;
        }

        if (mode == 0) {
            if (!out.empty()) {
                shapeMode.store(1, std::memory_order_relaxed);
                Logger::Info("[MovementSim] collision shapes verified ({} boxes)", out.size());
            } else {
                std::vector<AABB> grid;
                gridGather(region, query, grid);
                if (!grid.empty()) {
                    shapeMode.store(2, std::memory_order_relaxed);
                    Logger::Warn("[MovementSim] fetchAABBs returned empty where grid found {} solids, using "
                                 "solid-block grid",
                                 grid.size());
                    out = std::move(grid);
                }
            }
        }
    }

    float clipY(std::vector<AABB> const& shapes, AABB const& box, float dy) {
        for (auto const& s : shapes) {
            if (box.higher.x <= s.lower.x + eps || box.lower.x >= s.higher.x - eps) continue;
            if (box.higher.z <= s.lower.z + eps || box.lower.z >= s.higher.z - eps) continue;
            if (dy < 0.f && box.lower.y >= s.higher.y - eps) {
                float d = s.higher.y - box.lower.y;
                if (d > dy) dy = d;
            } else if (dy > 0.f && box.higher.y <= s.lower.y + eps) {
                float d = s.lower.y - box.higher.y;
                if (d < dy) dy = d;
            }
        }
        return dy;
    }

    float clipX(std::vector<AABB> const& shapes, AABB const& box, float dx) {
        for (auto const& s : shapes) {
            if (box.higher.y <= s.lower.y + eps || box.lower.y >= s.higher.y - eps) continue;
            if (box.higher.z <= s.lower.z + eps || box.lower.z >= s.higher.z - eps) continue;
            if (dx > 0.f && box.higher.x <= s.lower.x + eps) {
                float d = s.lower.x - box.higher.x;
                if (d < dx) dx = d;
            } else if (dx < 0.f && box.lower.x >= s.higher.x - eps) {
                float d = s.higher.x - box.lower.x;
                if (d > dx) dx = d;
            }
        }
        return dx;
    }

    float clipZ(std::vector<AABB> const& shapes, AABB const& box, float dz) {
        for (auto const& s : shapes) {
            if (box.higher.y <= s.lower.y + eps || box.lower.y >= s.higher.y - eps) continue;
            if (box.higher.x <= s.lower.x + eps || box.lower.x >= s.higher.x - eps) continue;
            if (dz > 0.f && box.higher.z <= s.lower.z + eps) {
                float d = s.lower.z - box.higher.z;
                if (d < dz) dz = d;
            } else if (dz < 0.f && box.lower.z >= s.higher.z - eps) {
                float d = s.higher.z - box.lower.z;
                if (d > dz) dz = d;
            }
        }
        return dz;
    }

    MovementSim::Result buildResult(SDK::BlockSource* region, std::vector<AABB> const& shapes, AABB const& box,
                                    int tick) {
        float feet = box.lower.y;
        float bestArea = 0.f;
        float ovMinX = box.lower.x;
        float ovMaxX = box.higher.x;
        float ovMinZ = box.lower.z;
        float ovMaxZ = box.higher.z;
        AABB const* bestShape = nullptr;

        for (auto const& s : shapes) {
            if (std::fabs(s.higher.y - feet) > 0.002f) continue;
            float oMinX = std::max(box.lower.x, s.lower.x);
            float oMaxX = std::min(box.higher.x, s.higher.x);
            float oMinZ = std::max(box.lower.z, s.lower.z);
            float oMaxZ = std::min(box.higher.z, s.higher.z);
            if (oMaxX - oMinX <= 0.f || oMaxZ - oMinZ <= 0.f) continue;
            float area = (oMaxX - oMinX) * (oMaxZ - oMinZ);
            if (area > bestArea) {
                bestArea = area;
                bestShape = &s;
                ovMinX = oMinX;
                ovMaxX = oMaxX;
                ovMinZ = oMinZ;
                ovMaxZ = oMaxZ;
            }
        }

        float cx = (ovMinX + ovMaxX) * 0.5f;
        float cz = (ovMinZ + ovMaxZ) * 0.5f;
        BlockPos block { floorI(cx), floorI(bestShape ? bestShape->lower.y + 0.01f : feet - 0.5f), floorI(cz) };

        bool liquid = blockIsLiquid(region, block.x, block.y + 1, block.z) ||
                      blockIsLiquid(region, block.x, block.y, block.z);

        return MovementSim::Result {
            block,
            feet,
            tick,
            Vec3 { cx, feet, cz },
            Vec2 { ovMinX, ovMinZ },
            Vec2 { ovMaxX, ovMaxZ },
            liquid,
        };
    }
}

bool MovementSim::isLiquidAt(SDK::BlockSource* region, BlockPos const& pos) {
    if (!region) return false;
    return blockIsLiquid(region, pos.x, pos.y, pos.z);
}

std::optional<MovementSim::Result> MovementSim::predictLanding(SDK::Actor* actor, int maxTicks) {
    if (!actor || !actor->aabbShape) return std::nullopt;
    auto& dim = actor->dimension;
    if (!dim) return std::nullopt;
    auto* region = dim->region;
    if (!region) return std::nullopt;

    AABB box = actor->getBoundingBox();
    Vec3 vel = actor->getVelocity();
    std::vector<AABB> shapes;
    shapes.reserve(64);

    for (int tick = 1; tick <= maxTicks; tick++) {
        vel.y = (vel.y - gravity) * verticalDrag;
        if (vel.y < terminalVelocity) vel.y = terminalVelocity;
        vel.x *= horizontalDrag;
        vel.z *= horizontalDrag;

        float reach = std::max({ NecromancerMath::abs(vel.x), NecromancerMath::abs(vel.y),
                                 NecromancerMath::abs(vel.z) }) +
                      0.5f;
        AABB query = box;
        query.lower.x -= reach;
        query.lower.y -= reach;
        query.lower.z -= reach;
        query.higher.x += reach;
        query.higher.y += reach;
        query.higher.z += reach;
        gatherShapes(region, query, shapes);

        float dy = clipY(shapes, box, vel.y);
        box.lower.y += dy;
        box.higher.y += dy;
        if (vel.y < 0.f && dy > vel.y + 0.000001f) {
            return buildResult(region, shapes, box, tick);
        }

        if (vel.y < 0.f) {
            int fy = floorI(box.lower.y);
            int fx0 = floorI(box.lower.x + inset);
            int fx1 = floorI(box.higher.x - inset);
            int fz0 = floorI(box.lower.z + inset);
            int fz1 = floorI(box.higher.z - inset);
            bool inLiquid = false;
            for (int lx = fx0; lx <= fx1 && !inLiquid; lx++) {
                for (int lz = fz0; lz <= fz1 && !inLiquid; lz++) {
                    if (blockIsLiquid(region, lx, fy, lz)) inLiquid = true;
                }
            }
            if (inLiquid) {
                float cx = (box.lower.x + box.higher.x) * 0.5f;
                float cz = (box.lower.z + box.higher.z) * 0.5f;
                return Result {
                    BlockPos { floorI(cx), fy, floorI(cz) },
                    box.lower.y,
                    tick,
                    Vec3 { cx, box.lower.y, cz },
                    Vec2 { box.lower.x, box.lower.z },
                    Vec2 { box.higher.x, box.higher.z },
                    true,
                };
            }
        }

        float dx = clipX(shapes, box, vel.x);
        box.lower.x += dx;
        box.higher.x += dx;
        if (NecromancerMath::abs(dx - vel.x) > 0.000001f) vel.x = 0.f;

        float dz = clipZ(shapes, box, vel.z);
        box.lower.z += dz;
        box.higher.z += dz;
        if (NecromancerMath::abs(dz - vel.z) > 0.000001f) vel.z = 0.f;

        if (box.lower.y < static_cast<float>(worldFloorY) - 4.f) return std::nullopt;
    }
    return std::nullopt;
}
