#pragma once
#include "mc/common/world/level/block/Block.h"
#include "mc/common/world/level/block/BlockLegacy.h"
#include "mc/common/world/level/BlockSource.h"
#include "util/LMath.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>

namespace BlockSolid {
    inline bool passesThrough(std::string const& id) {
        static const std::unordered_set<std::string> transparent = {
            "air", "cave_air", "void_air", "water", "flowing_water", "lava", "flowing_lava", "structure_void",
            "light_block", "tallgrass", "short_grass", "fern", "large_fern", "double_plant", "seagrass",
            "kelp", "vine", "torch", "unlit_redstone_torch", "redstone_torch", "soul_torch", "lantern",
            "rail", "golden_rail", "detector_rail", "activator_rail", "redstone_wire", "tripwire",
            "tripwire_hook", "ladder", "snow_layer", "carpet", "flower_pot", "sapling", "deadbush",
            "red_flower", "yellow_flower", "wheat", "carrots", "potatoes", "beetroot", "reeds", "sugar_cane",
            "web", "cobweb", "fire", "soul_fire", "nether_sprouts", "wither_rose", "crimson_roots",
            "warped_roots", "hanging_roots", "glow_lichen", "sculk_vein", "lily_pad", "waterlily",
            "end_rod", "chain", "button", "lever", "pressure_plate", "sign", "standing_sign", "wall_sign",
            "banner", "item_frame", "painting", "string", "bamboo_sapling",
        };
        return transparent.contains(id);
    }

    inline bool isLiquidName(std::string const& id) {
        return id == "water" || id == "flowing_water" || id == "lava" || id == "flowing_lava";
    }

    namespace detail {
        struct StateCache {
            static constexpr size_t capacity = 256;

            struct Entry {
                SDK::Block const* block = nullptr;
                bool passable = false;
                bool liquid = false;
            };

            std::array<Entry, capacity> entries {};
        };

        inline StateCache& stateCache() {
            thread_local StateCache instance {};
            return instance;
        }

        inline StateCache::Entry const& resolve(SDK::Block* block, SDK::BlockLegacy* legacy) {
            auto& store = stateCache();
            size_t index = static_cast<size_t>(((reinterpret_cast<uintptr_t>(block) >> 4) *
                                                0x9E3779B97F4A7C15ull) >>
                                               56) &
                           (StateCache::capacity - 1);

            auto& entry = store.entries[index];
            if (entry.block == block) return entry;

            std::string const id = legacy->name.getString();
            entry.block = block;
            entry.passable = passesThrough(id);
            entry.liquid = isLiquidName(id);
            return entry;
        }
    }

    inline bool isPassable(SDK::Block* block) {
        if (!block) return true;
        auto* legacy = block->legacyBlock;
        if (!legacy) return false;
        return detail::resolve(block, legacy).passable;
    }

    inline bool isLiquid(SDK::Block* block) {
        if (!block) return false;
        auto* legacy = block->legacyBlock;
        if (!legacy) return false;
        return detail::resolve(block, legacy).liquid;
    }

    inline bool isCollidable(SDK::BlockSource* region, int x, int y, int z) {
        if (!region) return false;
        auto* block = region->getBlock(BlockPos { x, y, z });
        if (!block) return false;
        if (!block->legacyBlock) return region->isSolidBlockingBlockAt(x, y, z);
        return !isPassable(block);
    }

    inline bool isLiquidAt(SDK::BlockSource* region, int x, int y, int z) {
        if (!region) return false;
        return isLiquid(region->getBlock(BlockPos { x, y, z }));
    }
}
