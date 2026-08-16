#include "pch.h"
#include "RenderFrameState.h"
#include "EntityCache.h"
#include "client/feature/module/modules/visual/AntiObs.h"
#include "client/event/Eventing.h"
#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/game/MinecraftGame.h"
#include "mc/common/client/gui/GuiData.h"
#include "mc/common/client/renderer/GameRenderer.h"
#include "mc/common/client/renderer/game/LevelRendererPlayer.h"
#include "mc/common/world/Minecraft.h"
#include "mc/common/world/actor/Actor.h"
#include <algorithm>

RenderFrameState::EntityGeometry const* RenderFrameState::Frame::find(uint64_t runtimeId) const {
    auto it = std::lower_bound(idIndex.begin(), idIndex.end(), runtimeId, [](auto const& entry, uint64_t value) {
        return entry.first < value;
    });
    if (it == idIndex.end() || it->first != runtimeId) return nullptr;
    return &entities[it->second];
}

RenderFrameState& RenderFrameState::get() {
    static auto* instance = new RenderFrameState;
    return *instance;
}

RenderFrameState::RenderFrameState() {
    for (auto& buffer : buffers) {
        buffer = std::make_shared<Frame>();
    }
    current.store(std::make_shared<const Frame>(), std::memory_order_release);
    Eventing::get().listen<RenderLevelEvent, &RenderFrameState::onRenderLevel>(this, 99);
    Eventing::get().listen<LeaveGameEvent, &RenderFrameState::onLeaveGame>(this, 99);
}

std::shared_ptr<RenderFrameState::Frame> RenderFrameState::acquireBuffer() {
    for (auto& buffer : buffers) {
        if (buffer.use_count() == 1) {
            buffer->entities.clear();
            buffer->idIndex.clear();
            return buffer;
        }
    }
    return std::make_shared<Frame>();
}

void RenderFrameState::onRenderLevel(Event&) {
    captures.fetch_add(1, std::memory_order_acq_rel);
    if (!AntiObs::isActive()) return;

    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return;

    auto lp = ci->getLocalPlayer();
    if (!lp) return;

    auto projection = WorldToScreen::createContext();
    if (!projection) return;

    auto gr = ci->minecraftGame ? ci->minecraftGame->gameRenderer : nullptr;
    if (!gr) return;

    auto frame = acquireBuffer();
    frame->projection = *projection;

    glm::mat4& viewMatrix = gr->lastViewMatrix._m;
    frame->camRight = Vec3 { viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0] }.normalized();

    frame->eyePos = lp->getPos();
    if (ci->levelRenderer && ci->levelRenderer->getLevelRendererPlayer()) {
        frame->eyePos = ci->levelRenderer->getLevelRendererPlayer()->getOrigin();
    }
    frame->playerYaw = lp->getRot().y;

    float alpha = ci->minecraft->timer ? ci->minecraft->timer->alpha : 1.f;

    auto snap = EntityCache::get().snapshot();
    if (snap) {
        frame->entities.reserve(snap->views.size());
        for (auto const& view : snap->views) {
            SDK::Actor* entt = view.actor;
            if (!entt) continue;

            EntityGeometry geo {};
            geo.actor = entt;
            geo.runtimeId = view.runtimeId;

            Vec3 const& pos = entt->getPos();
            Vec3 const& posOld = entt->getPosOld();
            geo.interpolatedPos = { std::lerp(posOld.x, pos.x, alpha), std::lerp(posOld.y, pos.y, alpha),
                                    std::lerp(posOld.z, pos.z, alpha) };

            AABB bb = entt->getBoundingBox();
            float eyeOffset = pos.y - bb.lower.y;
            bb.rebase(geo.interpolatedPos - Vec3 { 0.f, eyeOffset, 0.f } +
                      Vec3 { 0.f, (bb.higher.y - bb.lower.y) / 2.f, 0.f });
            geo.box = bb;
            geo.rot = entt->getRot();

            frame->entities.push_back(geo);
        }
    }

    frame->idIndex.resize(frame->entities.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(frame->entities.size()); ++i) {
        frame->idIndex[i] = { frame->entities[i].runtimeId, i };
    }
    std::sort(frame->idIndex.begin(), frame->idIndex.end(), [](auto const& a, auto const& b) {
        return a.first < b.first;
    });

    current.store(std::shared_ptr<const Frame>(frame), std::memory_order_release);
}

void RenderFrameState::onLeaveGame(Event&) {
    current.store(std::make_shared<const Frame>(), std::memory_order_release);
}
