#include "pch.h"
#include "TargetManager.h"
#include "mc/common/world/actor/Actor.h"

void TargetManager::setTarget(SDK::Actor* actor) {
    if (!actor) return;
    targetId.store(actor->getRuntimeID(), std::memory_order_release);
    lastSet.store(std::chrono::steady_clock::now(), std::memory_order_release);
}

void TargetManager::clearTarget(SDK::Actor* actor) {
    if (actor) clearTarget(actor->getRuntimeID());
}

void TargetManager::clearTarget(uint64_t runtimeId) {
    uint64_t expected = runtimeId;
    if (expected != 0) targetId.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
}

void TargetManager::clearAll() {
    targetId.store(0, std::memory_order_release);
}

bool TargetManager::isTargetedId(uint64_t runtimeId) {
    uint64_t id = targetId.load(std::memory_order_acquire);
    if (id == 0) return false;
    if (std::chrono::steady_clock::now() - lastSet.load(std::memory_order_acquire) > expiry) {
        uint64_t expected = id;
        targetId.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
        return false;
    }
    return runtimeId == id;
}

bool TargetManager::isTargeted(SDK::Actor* actor) {
    if (!actor) return false;
    return isTargetedId(actor->getRuntimeID());
}
