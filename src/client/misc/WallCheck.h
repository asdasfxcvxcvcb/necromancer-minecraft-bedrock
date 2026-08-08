#pragma once
#include "BlockSolid.h"
#include "mc/common/world/level/block/Block.h"
#include "mc/common/world/level/block/BlockLegacy.h"
#include "mc/common/world/level/BlockSource.h"
#include "util/LMath.h"
#include "util/Logger.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace WallCheck {
    namespace detail {
        constexpr size_t fetchAABBsVtIndex = 0x10;

        inline std::atomic<int>& shapeMode() {
            static std::atomic<int> mode { 0 };
            return mode;
        }

        inline bool fetchShapesRaw(SDK::BlockSource* region, std::vector<AABB>* out, AABB const* query) {
            __try {
                using Fn = void(__fastcall*)(void*, std::vector<AABB>&, AABB const&, bool);
                Fn* vtable = *reinterpret_cast<Fn**>(region);
                vtable[fetchAABBsVtIndex](region, *out, *query, false);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        inline bool validateShapes(std::vector<AABB> const& shapes, AABB const& query) {
            if (shapes.size() > 512) return false;
            for (auto const& s : shapes) {
                if (!std::isfinite(s.lower.x) || !std::isfinite(s.lower.y) || !std::isfinite(s.lower.z) ||
                    !std::isfinite(s.higher.x) || !std::isfinite(s.higher.y) || !std::isfinite(s.higher.z)) {
                    return false;
                }
                if (s.higher.x < s.lower.x || s.higher.y < s.lower.y || s.higher.z < s.lower.z) return false;
                if (s.higher.x - s.lower.x > 64.f || s.higher.y - s.lower.y > 64.f ||
                    s.higher.z - s.lower.z > 64.f) {
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

        struct SolidCache {
            static constexpr size_t capacity = 1024;

            struct Entry {
                int64_t key = std::numeric_limits<int64_t>::min();
                uint32_t generation = 0;
                bool solid = false;
            };

            std::array<Entry, capacity> entries {};
            uint32_t generation = 1;
        };

        inline SolidCache& cache() {
            thread_local SolidCache instance {};
            return instance;
        }

        inline int64_t packKey(int x, int y, int z) {
            return (static_cast<int64_t>(x) & 0x3FFFFFF) | ((static_cast<int64_t>(z) & 0x3FFFFFF) << 26) |
                   ((static_cast<int64_t>(y) & 0xFFF) << 52);
        }

        inline bool isSolid(SDK::BlockSource* region, int x, int y, int z) {
            auto& store = cache();
            int64_t key = packKey(x, y, z);
            size_t index = static_cast<size_t>((static_cast<uint64_t>(key) * 0x9E3779B97F4A7C15ull) >> 54) &
                           (SolidCache::capacity - 1);

            auto& entry = store.entries[index];
            if (entry.generation == store.generation && entry.key == key) return entry.solid;

            bool solid = BlockSolid::isCollidable(region, x, y, z);

            entry.key = key;
            entry.generation = store.generation;
            entry.solid = solid;
            return solid;
        }

        enum class ShapeResult {
            Clear,
            Blocked,
            Unavailable,
        };

        constexpr float chunkLength = 4.f;

        inline bool gridHasSolid(SDK::BlockSource* region, AABB const& query) {
            int x0 = static_cast<int>(std::floor(query.lower.x));
            int x1 = static_cast<int>(std::floor(query.higher.x));
            int y0 = static_cast<int>(std::floor(query.lower.y));
            int y1 = static_cast<int>(std::floor(query.higher.y));
            int z0 = static_cast<int>(std::floor(query.lower.z));
            int z1 = static_cast<int>(std::floor(query.higher.z));
            if (static_cast<long long>(x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > 4096) return false;
            for (int x = x0; x <= x1; ++x) {
                for (int y = y0; y <= y1; ++y) {
                    for (int z = z0; z <= z1; ++z) {
                        if (isSolid(region, x, y, z)) return true;
                    }
                }
            }
            return false;
        }

        inline ShapeResult segmentHitsShapes(SDK::BlockSource* region, Vec3 const& from, Vec3 const& dir,
                                             float travel) {
            thread_local std::vector<AABB> shapes;

            for (float start = 0.f; start < travel; start += chunkLength) {
                float span = std::min(chunkLength, travel - start);
                Vec3 a { from.x + dir.x * start, from.y + dir.y * start, from.z + dir.z * start };
                Vec3 b { a.x + dir.x * span, a.y + dir.y * span, a.z + dir.z * span };

                AABB query {
                    Vec3 { std::min(a.x, b.x) - 0.5f, std::min(a.y, b.y) - 0.5f, std::min(a.z, b.z) - 0.5f },
                    Vec3 { std::max(a.x, b.x) + 0.5f, std::max(a.y, b.y) + 0.5f, std::max(a.z, b.z) + 0.5f },
                };

                shapes.clear();
                if (!fetchShapesRaw(region, &shapes, &query)) {
                    shapeMode().store(2, std::memory_order_relaxed);
                    return ShapeResult::Unavailable;
                }
                if (!validateShapes(shapes, query)) return ShapeResult::Unavailable;

                if (shapes.empty()) {
                    if (gridHasSolid(region, query)) {
                        shapeMode().store(2, std::memory_order_relaxed);
                        Logger::Warn("[WallCheck] fetchAABBs returned empty where grid found solids, "
                                     "falling back to solid-block raycast");
                        return ShapeResult::Unavailable;
                    }
                    continue;
                }

                if (shapeMode().load(std::memory_order_relaxed) == 0) {
                    shapeMode().store(1, std::memory_order_relaxed);
                    Logger::Info("[WallCheck] collision shapes verified ({} boxes)", shapes.size());
                }

                for (auto const& s : shapes) {
                    if (s.intersectsRay(a, dir, span).has_value()) return ShapeResult::Blocked;
                }
            }

            return ShapeResult::Clear;
        }
    }

    inline void beginPass() {
        ++detail::cache().generation;
    }

    inline bool isVisible(SDK::BlockSource* region, Vec3 const& from, Vec3 const& to, float shorten = 0.1f) {
        if (!region) return true;

        Vec3 delta = to - from;
        float distance = delta.magnitude();
        if (!std::isfinite(distance) || distance <= 0.0001f) return true;
        if (!std::isfinite(from.x) || !std::isfinite(from.y) || !std::isfinite(from.z)) return true;

        float travel = std::min(distance - shorten, 256.f);
        if (travel <= 0.f) return true;

        float inverse = 1.f / distance;
        Vec3 dir { delta.x * inverse, delta.y * inverse, delta.z * inverse };

        if (detail::shapeMode().load(std::memory_order_relaxed) != 2 && travel <= 24.f) {
            auto result = detail::segmentHitsShapes(region, from, dir, travel);
            if (result == detail::ShapeResult::Blocked) return false;
            if (result == detail::ShapeResult::Clear) return true;
        }

        float dirX = dir.x;
        float dirY = dir.y;
        float dirZ = dir.z;

        int x = static_cast<int>(std::floor(from.x));
        int y = static_cast<int>(std::floor(from.y));
        int z = static_cast<int>(std::floor(from.z));

        constexpr float unreachable = std::numeric_limits<float>::max();

        int stepX = dirX > 0.f ? 1 : (dirX < 0.f ? -1 : 0);
        int stepY = dirY > 0.f ? 1 : (dirY < 0.f ? -1 : 0);
        int stepZ = dirZ > 0.f ? 1 : (dirZ < 0.f ? -1 : 0);

        float spanX = stepX != 0 ? std::fabs(1.f / dirX) : unreachable;
        float spanY = stepY != 0 ? std::fabs(1.f / dirY) : unreachable;
        float spanZ = stepZ != 0 ? std::fabs(1.f / dirZ) : unreachable;

        auto toBoundary = [](float origin, int cell, int step) {
            return step > 0 ? (static_cast<float>(cell) + 1.f - origin) : (origin - static_cast<float>(cell));
        };

        float nextX = stepX != 0 ? toBoundary(from.x, x, stepX) * spanX : unreachable;
        float nextY = stepY != 0 ? toBoundary(from.y, y, stepY) * spanY : unreachable;
        float nextZ = stepZ != 0 ? toBoundary(from.z, z, stepZ) * spanZ : unreachable;

        int budget = static_cast<int>(travel * 3.f) + 8;
        while (budget-- > 0) {
            float reached;
            if (nextX < nextY) {
                if (nextX < nextZ) {
                    x += stepX;
                    reached = nextX;
                    nextX += spanX;
                } else {
                    z += stepZ;
                    reached = nextZ;
                    nextZ += spanZ;
                }
            } else {
                if (nextY < nextZ) {
                    y += stepY;
                    reached = nextY;
                    nextY += spanY;
                } else {
                    z += stepZ;
                    reached = nextZ;
                    nextZ += spanZ;
                }
            }

            if (reached > travel) return true;
            if (detail::isSolid(region, x, y, z)) return false;
        }

        return true;
    }
}
