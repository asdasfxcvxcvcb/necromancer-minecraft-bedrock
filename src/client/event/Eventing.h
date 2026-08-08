#pragma once
#include "client/event/Event.h"
#include "client/event/Listener.h"
#include "client/misc/ModuleProfilerHook.h"
#include <bit>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <shared_mutex>
#include <vector>
#include <algorithm>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>

class Eventing final {
public:
    Eventing() = default;
    ~Eventing() = default;

    template<typename T>
    bool dispatch(T&& ev)
        requires std::derived_from<std::remove_reference_t<T>, Event>
    {
        using EventType = std::remove_reference_t<T>;

        auto& scratch = tlsScratch();
        auto& depth = tlsDepth();

        if (scratch.size() <= depth) scratch.emplace_back();
        auto& snapshot = scratch[depth];

        {
            std::shared_lock lock { mutex };
            auto bucketIt = this->buckets.find(EventType::hash);
            if (bucketIt == this->buckets.end() || bucketIt->second.empty()) return false;
            snapshot.assign(bucketIt->second.begin(), bucketIt->second.end());
        }

        struct DepthGuard {
            std::vector<Registration>& snap;
            size_t& depth;
            ~DepthGuard() {
                snap.clear();
                --depth;
            }
        } guard { snapshot, depth };
        ++depth;

        for (auto const& registration : snapshot) {
            auto const& state = registration.state;
            std::lock_guard invocationLock { state->invocationMutex };

            if (!state->active || !state->listener) {
                continue;
            }

            if (registration.callback.callWhileInactive || state->listener->shouldListen()) {
                const bool isCancel = ev.isCancellable();

                int profSlot = -1;
                std::chrono::steady_clock::time_point profStart;
                if (profiler_hook::enabled.load(std::memory_order_relaxed)) {
                    auto* listener = state->listener;
                    uint32_t epoch = profiler_hook::epoch.load(std::memory_order_relaxed);
                    if (listener->profilerSlotCache == -2 || listener->profilerEpoch != epoch) {
                        listener->profilerSlotCache = profiler_hook::beginSlot(listener->profilerName());
                        listener->profilerEpoch = epoch;
                    }
                    profSlot = listener->profilerSlotCache;
                    if (profSlot >= 0) profStart = std::chrono::steady_clock::now();
                }

                (state->listener->*registration.callback.fptr)(ev);

                if (profSlot >= 0) {
                    auto elapsed = std::chrono::steady_clock::now() - profStart;
                    profiler_hook::record(
                        profSlot,
                        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()));
                }

                if (isCancel) {
                    auto& cEv = reinterpret_cast<Cancellable&>(ev);
                    if (cEv.isCancelled()) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // DO NOT USE, use listen<Event, &Listener::func> instead
    template<typename T>
    void listen(Listener* ptr, EventListenerFunc listener, int priority = 0, bool callWhileInactive = false)
        requires std::derived_from<T, Event>
    {
        addListener(T::hash, EventListener { listener, ptr, callWhileInactive, priority });
    }

    template<typename T, auto listener>
    void listen(Listener* ptr, int priority = 0, bool callWhileInactive = false)
        requires std::derived_from<T, Event>
    {
        struct MFPtr {
            const EventListenerFunc ptr;
            const ptrdiff_t adj;
        };

        if constexpr (sizeof(listener) == sizeof(EventListenerFunc)) {
            addListener(T::hash,
                        EventListener { static_cast<EventListenerFunc>(listener), ptr, callWhileInactive, priority });
        } else if constexpr (sizeof(listener) == sizeof(MFPtr)) {
            const MFPtr mfp = std::bit_cast<MFPtr>(listener);

            addListener(T::hash, EventListener { mfp.ptr, ptr, callWhileInactive, priority });
        } else {
            static_assert(false, "Unsupported listener function type");
        }
    }

    void unlisten(Listener* ptr) {
        if (!ptr) {
            return;
        }

        std::shared_ptr<ListenerState> state;
        {
            std::unique_lock lock { mutex };
            auto stateIt = listenerStates.find(ptr);
            if (stateIt == listenerStates.end()) {
                return;
            }

            state = std::move(stateIt->second);
            listenerStates.erase(stateIt);
            for (auto& [hash, bucket] : buckets) {
                std::erase_if(bucket, [&](Registration const& registration) {
                    return registration.state == state;
                });
            }
        }

        std::lock_guard invocationLock { state->invocationMutex };
        state->active = false;
        state->listener = nullptr;
    }

    // Substitute for Necromancer::getEventing
    [[nodiscard]] static Eventing& get();

private:
    struct ListenerState {
        explicit ListenerState(Listener* listener)
            : listener(listener) {}

        std::recursive_mutex invocationMutex;
        Listener* listener;
        bool active = true;
    };

    struct Registration {
        uint32_t hash;
        EventListener callback;
        std::shared_ptr<ListenerState> state;
    };

    static std::deque<std::vector<Registration>>& tlsScratch() {
        thread_local std::deque<std::vector<Registration>> scratch;
        return scratch;
    }

    static size_t& tlsDepth() {
        thread_local size_t depth = 0;
        return depth;
    }

    void addListener(uint32_t hash, EventListener callback) {
        if (!callback.listener || callback.fptr == nullptr) {
            return;
        }

        std::unique_lock lock { mutex };
        auto [stateIt, inserted] = listenerStates.try_emplace(callback.listener);
        if (inserted) {
            stateIt->second = std::make_shared<ListenerState>(callback.listener);
        }

        auto& bucket = buckets[hash];
        bucket.push_back({ hash, callback, stateIt->second });
        std::stable_sort(bucket.begin(), bucket.end(), [](Registration const& left, Registration const& right) {
            return left.callback.priority > right.callback.priority;
        });
    }

    std::shared_mutex mutex;
    std::unordered_map<uint32_t, std::vector<Registration>> buckets;
    std::unordered_map<Listener*, std::shared_ptr<ListenerState>> listenerStates;
};
