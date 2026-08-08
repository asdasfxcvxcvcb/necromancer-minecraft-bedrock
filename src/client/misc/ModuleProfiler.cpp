#include "pch.h"
#include "ModuleProfiler.h"
#include "ModuleProfilerHook.h"
#include "util/Util.h"
#include "client/Necromancer.h"
#include "client/misc/Timings.h"
#include <cstring>
#include <fstream>

ModuleProfiler& ModuleProfiler::get() {
    static auto* instance = new ModuleProfiler;
    return *instance;
}

ModuleProfiler::~ModuleProfiler() {
}

void ModuleProfiler::setEnabled(bool on) {
    bool was = enabled.exchange(on, std::memory_order_acq_rel);
    if (on && !was) {
        for (int i = 0; i < slotCount.load(std::memory_order_acquire); ++i) {
            slots[i].calls.store(0, std::memory_order_relaxed);
            slots[i].nanos.store(0, std::memory_order_relaxed);
        }
        windowStart = std::chrono::steady_clock::now();
        profiler_hook::epoch.fetch_add(1, std::memory_order_release);
        truncateReport();
        startWorker();
        writeReport();
    } else if (!on && was) {
        stopWorker();
    }
    profiler_hook::enabled.store(on, std::memory_order_release);
}

int ModuleProfiler::slotFor(char const* internedName) {
    if (!internedName) return -1;

    int count = slotCount.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) {
        if (slots[i].name == internedName) return i;
    }

    std::lock_guard lock { slotMutex };
    count = slotCount.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) {
        if (slots[i].name == internedName) return i;
    }
    if (count >= static_cast<int>(maxSlots)) return -1;

    slots[count].name = internedName;
    slots[count].calls.store(0, std::memory_order_relaxed);
    slots[count].nanos.store(0, std::memory_order_relaxed);
    slotCount.store(count + 1, std::memory_order_release);
    return count;
}

void ModuleProfiler::record(int slot, uint64_t nanos) {
    if (slot < 0 || slot >= static_cast<int>(maxSlots)) return;
    slots[slot].calls.fetch_add(1, std::memory_order_relaxed);
    slots[slot].nanos.fetch_add(nanos, std::memory_order_relaxed);
}

void ModuleProfiler::startWorker() {
    if (workerRunning.exchange(true, std::memory_order_acq_rel)) return;
    stopRequested.store(false, std::memory_order_release);
    worker = std::thread(&ModuleProfiler::workerLoop, this);
}

void ModuleProfiler::stopWorker() {
    if (!workerRunning.exchange(false, std::memory_order_acq_rel)) return;
    {
        std::lock_guard lock { cvMutex };
        stopRequested.store(true, std::memory_order_release);
    }
    cv.notify_all();
    if (worker.joinable()) worker.join();
}

void ModuleProfiler::shutdown() {
    enabled.store(false, std::memory_order_release);
    profiler_hook::enabled.store(false, std::memory_order_release);
    stopWorker();
}

void ModuleProfiler::workerLoop() {
    while (true) {
        std::unique_lock lock { cvMutex };
        cv.wait_for(lock, std::chrono::seconds(10),
                    [this] { return stopRequested.load(std::memory_order_acquire); });
        if (stopRequested.load(std::memory_order_acquire)) break;
        lock.unlock();

        writeReport();
    }
}

void ModuleProfiler::truncateReport() {
    std::error_code ec;
    auto dir = util::GetNecromancerPath() / "Logs";
    std::filesystem::create_directories(dir, ec);
    std::ofstream ofs(dir / "fps_tester.txt", std::ios::trunc);
}

void ModuleProfiler::writeReport() {
    auto now = std::chrono::steady_clock::now();
    float windowSec = std::chrono::duration<float>(now - windowStart).count();
    windowStart = now;
    if (windowSec <= 0.f) windowSec = 1.f;

    struct Row {
        char const* name;
        uint64_t calls;
        uint64_t nanos;
    };
    Row rows[maxSlots];
    int count = slotCount.load(std::memory_order_acquire);
    if (count > static_cast<int>(maxSlots)) count = static_cast<int>(maxSlots);

    for (int i = 0; i < count; ++i) {
        rows[i].name = slots[i].name;
        rows[i].calls = slots[i].calls.exchange(0, std::memory_order_relaxed);
        rows[i].nanos = slots[i].nanos.exchange(0, std::memory_order_relaxed);
    }

    std::error_code ec;
    auto dir = util::GetNecromancerPath() / "Logs";
    std::filesystem::create_directories(dir, ec);

    std::ofstream ofs(dir / "fps_tester.txt", std::ios::app);
    if (!ofs.is_open()) return;

    std::time_t t = std::time(nullptr);
    std::tm tmv {};
    localtime_s(&tmv, &t);
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    int fps = Necromancer::get().getTimings().getFPS();
    float frameTime = Necromancer::get().getTimings().getFrameTime();

    ofs << "==== [" << stamp << "] window " << windowSec << "s  fps=" << fps << "  frametime=" << frameTime
        << "ms ====\n";

    for (int i = 0; i < count; ++i) {
        if (rows[i].calls == 0) continue;
        double totalMs = static_cast<double>(rows[i].nanos) / 1.0e6;
        double avgUs = static_cast<double>(rows[i].nanos) / 1.0e3 / static_cast<double>(rows[i].calls);
        char line[256];
        std::snprintf(line, sizeof(line), "  %-24s calls=%-8llu total=%9.3fms avg=%8.2fus\n",
                      rows[i].name ? rows[i].name : "?", static_cast<unsigned long long>(rows[i].calls), totalMs,
                      avgUs);
        ofs << line;
    }
    ofs << "\n";
}

namespace profiler_hook {
    int beginSlot(char const* internedName) {
        if (!enabled.load(std::memory_order_relaxed) || !internedName) return -1;
        return ModuleProfiler::get().slotFor(internedName);
    }

    void record(int slot, uint64_t nanos) {
        if (slot < 0) return;
        ModuleProfiler::get().record(slot, nanos);
    }
}
