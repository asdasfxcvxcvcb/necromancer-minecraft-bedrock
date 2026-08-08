#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

namespace SDK {
    class Actor;
}

class TargetManager {
public:
    static void setTarget(SDK::Actor* actor);
    static void clearTarget(SDK::Actor* actor);
    static void clearTarget(uint64_t runtimeId);
    static void clearAll();
    static bool isTargeted(SDK::Actor* actor);
    static bool isTargetedId(uint64_t runtimeId);
    static uint64_t getTargetId() { return targetId.load(std::memory_order_acquire); }

private:
    inline static std::atomic<uint64_t> targetId { 0 };
    inline static std::atomic<std::chrono::steady_clock::time_point> lastSet {};
    static constexpr std::chrono::milliseconds expiry { 300 };
};
