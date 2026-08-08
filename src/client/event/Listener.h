#pragma once

// Abstract class
class Listener {
public:
    Listener() = default;
    virtual ~Listener() = default;

    virtual bool shouldListen() { return true; }

    // Stable, interned C-string identifying this listener for the module
    // profiler. Returns nullptr for listeners that should not be profiled.
    virtual char const* profilerName() { return nullptr; }

    // Resolved profiler slot, cached so dispatch does not re-scan the slot table
    // on every single listener invocation. -2 means "not resolved yet", -1 means
    // "not profileable". Reset when profiling is switched on.
    int profilerSlotCache = -2;
    uint32_t profilerEpoch = 0;
};
