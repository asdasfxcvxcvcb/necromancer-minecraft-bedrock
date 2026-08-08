#include "pch.h"
#include "EntityCache.h"
#include "client/event/Eventing.h"
#include "client/feature/module/modules/misc/AntiBot.h"
#include "mc/common/world/level/Level.h"
#include <algorithm>
#include <mutex>

namespace {
    std::mutex rebuildMutex;
}

void EntityCache::Snapshot::clear() {
    actors.clear();
    views.clear();
    idIndex.clear();
}

void EntityCache::Snapshot::buildIndex() {
    idIndex.resize(views.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(views.size()); ++i) {
        idIndex[i] = { views[i].runtimeId, i };
    }
    std::sort(idIndex.begin(), idIndex.end(), [](auto const& a, auto const& b) {
        return a.first < b.first;
    });
}

EntityCache::EntityView const* EntityCache::Snapshot::findView(uint64_t runtimeId) const {
    auto it = std::lower_bound(idIndex.begin(), idIndex.end(), runtimeId, [](auto const& entry, uint64_t value) {
        return entry.first < value;
    });
    if (it == idIndex.end() || it->first != runtimeId) return nullptr;
    return &views[it->second];
}

SDK::Actor* EntityCache::Snapshot::findActor(uint64_t runtimeId) const {
    auto const* view = findView(runtimeId);
    return view ? view->actor : nullptr;
}

EntityCache& EntityCache::get() {
    static auto* instance = new EntityCache;
    return *instance;
}

EntityCache::EntityCache() {
    for (auto& buffer : buffers) {
        buffer = std::make_shared<Snapshot>();
    }
    current.store(std::make_shared<const Snapshot>(), std::memory_order_release);
    Eventing::get().listen<TickEvent, &EntityCache::onTick>(this, 100);
    Eventing::get().listen<RenderLevelEvent, &EntityCache::onRenderLevel>(this, 100);
    Eventing::get().listen<LeaveGameEvent, &EntityCache::onLeaveGame>(this, 100);
}

std::shared_ptr<EntityCache::Snapshot> EntityCache::acquireBuffer() {
    for (auto& buffer : buffers) {
        if (buffer.use_count() == 1) {
            buffer->clear();
            return buffer;
        }
    }
    return std::make_shared<Snapshot>();
}

void EntityCache::rebuild() {
    std::lock_guard lock { rebuildMutex };

    auto snap = acquireBuffer();

    auto ci = SDK::ClientInstance::get();
    if (ci && ci->minecraft) {
        if (auto level = ci->minecraft->getLevel()) {
            scratch.clear();
            level->getRuntimeActorList(scratch);

            snap->actors.reserve(scratch.size());
            snap->views.reserve(scratch.size());

            for (auto entt : scratch) {
                if (!entt) continue;
                if (AntiBot::isBot(entt)) continue;

                EntityView view {};
                view.actor = entt;
                view.runtimeId = entt->getRuntimeID();
                view.typeId = entt->getEntityTypeID();
                view.isPlayer = view.typeId == 319;
                view.isItem = view.typeId == 64;
                view.invisible = entt->isInvisible();

                if (auto health = entt->getHealth()) {
                    view.hasHealth = true;
                    view.health = *health;
                    view.maxHealth = entt->getMaxHealth().value_or(0.f);
                }

                if (view.isPlayer) view.kind = EntKind::Player;
                else if (view.isItem) view.kind = EntKind::Item;
                else view.kind = view.hasHealth ? EntKind::Mob : EntKind::Other;

                snap->actors.push_back(entt);
                snap->views.push_back(view);
            }

            snap->buildIndex();
        }
    }

    current.store(std::shared_ptr<const Snapshot>(snap), std::memory_order_release);
    lastRebuild.store(std::chrono::steady_clock::now(), std::memory_order_release);
}

std::shared_ptr<const EntityCache::Snapshot> EntityCache::snapshot() {
    return current.load(std::memory_order_acquire);
}

SDK::Actor* EntityCache::findByRuntimeID(uint64_t runtimeId) {
    auto snap = current.load(std::memory_order_acquire);
    if (!snap) return nullptr;
    return snap->findActor(runtimeId);
}

void EntityCache::onTick(Event&) {
    rebuild();
}

void EntityCache::onRenderLevel(Event&) {
    auto now = std::chrono::steady_clock::now();
    if (now - lastRebuild.load(std::memory_order_acquire) < renderStaleAfter) return;
    rebuild();
}

void EntityCache::onLeaveGame(Event&) {
    current.store(std::make_shared<const Snapshot>(), std::memory_order_release);
}
