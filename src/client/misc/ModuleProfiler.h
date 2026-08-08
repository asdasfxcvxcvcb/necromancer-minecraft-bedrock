#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

class ModuleProfiler {
public:
    static constexpr size_t maxSlots = 128;

    struct Slot {
        char const* name = nullptr;
        std::atomic<uint64_t> calls { 0 };
        std::atomic<uint64_t> nanos { 0 };
    };

    static ModuleProfiler& get();

    void setEnabled(bool on);
    [[nodiscard]] bool isEnabled() const { return enabled.load(std::memory_order_relaxed); }

    int slotFor(char const* internedName);
    void record(int slot, uint64_t nanos);

    void shutdown();

    class ScopeTimer {
    public:
        explicit ScopeTimer(int slot)
            : slot(slot) {
            if (slot >= 0) start = std::chrono::steady_clock::now();
        }
        ~ScopeTimer() {
            if (slot < 0) return;
            auto elapsed = std::chrono::steady_clock::now() - start;
            ModuleProfiler::get().record(
                slot, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()));
        }
        ScopeTimer(ScopeTimer const&) = delete;
        ScopeTimer& operator=(ScopeTimer const&) = delete;

    private:
        int slot;
        std::chrono::steady_clock::time_point start;
    };

private:
    ModuleProfiler() = default;
    ~ModuleProfiler();

    void startWorker();
    void stopWorker();
    void workerLoop();
    void writeReport();
    void truncateReport();

    std::array<Slot, maxSlots> slots {};
    std::atomic<int> slotCount { 0 };
    std::mutex slotMutex;

    std::atomic<bool> enabled { false };
    std::atomic<bool> workerRunning { false };
    std::atomic<bool> stopRequested { false };
    std::thread worker;
    std::mutex cvMutex;
    std::condition_variable cv;
    std::chrono::steady_clock::time_point windowStart;
};

#define NECRO_PROFILE_SCOPE(slotId) ModuleProfiler::ScopeTimer necroProfScope_##__LINE__ { slotId }
