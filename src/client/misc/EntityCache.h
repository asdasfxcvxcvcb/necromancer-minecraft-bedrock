#pragma once
#include "client/event/Listener.h"
#include "client/event/Event.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace SDK {
    class Actor;
}

class EntityCache : public Listener {
public:
    enum class EntKind : uint8_t {
        Player,
        Item,
        Mob,
        Other
    };

    struct EntityView {
        SDK::Actor* actor = nullptr;
        uint64_t runtimeId = 0;
        float health = 0.f;
        float maxHealth = 0.f;
        uint32_t typeId = 0;
        EntKind kind = EntKind::Other;
        bool isPlayer = false;
        bool isItem = false;
        bool invisible = false;
        bool hasHealth = false;
    };

    struct Snapshot {
        std::vector<SDK::Actor*> actors;
        std::vector<EntityView> views;
        std::vector<std::pair<uint64_t, uint32_t>> idIndex;

        [[nodiscard]] EntityView const* findView(uint64_t runtimeId) const;
        [[nodiscard]] SDK::Actor* findActor(uint64_t runtimeId) const;
        void clear();
        void buildIndex();
    };

    static EntityCache& get();

    std::shared_ptr<const Snapshot> snapshot();
    SDK::Actor* findByRuntimeID(uint64_t runtimeId);

    void onTick(Event& ev);
    void onRenderLevel(Event& ev);
    void onLeaveGame(Event& ev);

private:
    EntityCache();
    void rebuild();
    std::shared_ptr<Snapshot> acquireBuffer();

    static constexpr size_t bufferCount = 4;
    static constexpr std::chrono::steady_clock::duration renderStaleAfter = std::chrono::milliseconds(16);

    std::atomic<std::shared_ptr<const Snapshot>> current;
    std::atomic<std::chrono::steady_clock::time_point> lastRebuild {};
    std::shared_ptr<Snapshot> buffers[bufferCount];
    std::vector<SDK::Actor*> scratch;
};
