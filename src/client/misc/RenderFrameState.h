#pragma once
#include "client/event/Event.h"
#include "client/event/Listener.h"
#include "util/WorldToScreen.h"
#include "util/LMath.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace SDK {
    class Actor;
}

class RenderFrameState : public Listener {
public:
    struct EntityGeometry {
        SDK::Actor* actor = nullptr;
        uint64_t runtimeId = 0;
        Vec3 interpolatedPos {};
        AABB box {};
        Vec2 rot {};
    };

    struct Frame {
        WorldToScreen::ProjectionContext projection {};
        Vec3 camRight {};
        Vec3 eyePos {};
        float playerYaw = 0.f;
        std::vector<EntityGeometry> entities;
        std::vector<std::pair<uint64_t, uint32_t>> idIndex;

        [[nodiscard]] EntityGeometry const* find(uint64_t runtimeId) const;
    };

    static RenderFrameState& get();

    [[nodiscard]] std::shared_ptr<const Frame> latest() const {
        return current.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint64_t captureCount() const { return captures.load(std::memory_order_acquire); }

    void onRenderLevel(Event& ev);
    void onLeaveGame(Event& ev);

private:
    RenderFrameState();
    std::shared_ptr<Frame> acquireBuffer();

    static constexpr size_t bufferCount = 3;
    std::atomic<std::shared_ptr<const Frame>> current;
    std::atomic<uint64_t> captures { 0 };
    std::shared_ptr<Frame> buffers[bufferCount];
};
