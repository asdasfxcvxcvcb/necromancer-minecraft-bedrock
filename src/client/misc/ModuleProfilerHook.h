#pragma once
#include <atomic>
#include <cstdint>

// Lightweight facade so the tree-wide Eventing.h dispatch hot path can gate
// profiling on a single relaxed atomic load, without pulling the profiler's
// threading/chrono headers into every translation unit.
namespace profiler_hook {
    inline std::atomic<bool> enabled { false };
    // Bumped every time profiling is switched on, so per-listener slot caches from
    // a previous session are invalidated without walking every listener.
    inline std::atomic<uint32_t> epoch { 1 };

    // Returns a slot index for the interned name, or -1 (not profiling / null).
    int beginSlot(char const* internedName);
    void record(int slot, uint64_t nanos);
}
