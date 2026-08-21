#include "pch.h"
#include "Reach.h"
#include "Backtrack.h"
#include "AfterTrack.h"

#include "client/misc/EntityCache.h"
#include "client/misc/PlayerListManager.h"
#include "client/misc/TargetManager.h"
#include "client/misc/WallCheck.h"
#include <client/screen/ScreenManager.h>
#include <client/event/events/ClickEvent.h>
#include <client/event/events/AfterMoveEvent.h>
#include <mc/Addresses.h>
#include <mc/common/client/game/ClientInstance.h>
#include <mc/common/client/game/MinecraftGame.h>
#include <mc/common/client/player/LocalPlayer.h>
#include <mc/common/world/Minecraft.h>
#include <mc/common/world/level/Level.h>
#include <mc/common/world/level/HitResult.h>
#include <mc/common/world/level/BlockSource.h>
#include <mc/common/world/actor/Actor.h>
#include <mc/common/world/actor/player/Player.h>
#include <mc/common/world/actor/player/GameMode.h>

namespace {
    constexpr float vanillaReach = 3.f;

    Vec3 rayDirectionFromHit(SDK::HitResult* hit, bool ignoreWalls) {
        if (ignoreWalls) {
            return hit->end.magnitude() > 0.0001f ? hit->end.normalized() : Vec3 { 0.f, 0.f, 0.f };
        }
        Vec3 toHit = hit->hitPos - hit->start;
        if (toHit.magnitude() > 0.001f) return toHit.normalized();
        return hit->end.magnitude() > 0.0001f ? hit->end.normalized() : Vec3 { 0.f, 0.f, 0.f };
    }
}

Reach::Reach()
    : Module("Reach", LocalizeString::get("client.module.reach.name"),
             LocalizeString::get("client.module.reach.desc"), GAME, nokeybind) {
    addSliderSetting("range", LocalizeString::get("client.module.reach.range.name"),
                     LocalizeString::get("client.module.reach.range.desc"), range, FloatValue(3.f), FloatValue(10.f),
                     FloatValue(0.1f));
    addSetting("players", LocalizeString::get("client.module.reach.players.name"),
               LocalizeString::get("client.module.reach.players.desc"), players);
    addSetting("mobs", LocalizeString::get("client.module.reach.mobs.name"),
               LocalizeString::get("client.module.reach.mobs.desc"), mobs);
    addSetting("ignoreFriends", LocalizeString::get("client.module.reach.ignoreFriends.name"),
               LocalizeString::get("client.module.reach.ignoreFriends.desc"), ignoreFriends);
    addSetting("wallCheck", LocalizeString::get("client.module.reach.wallCheck.name"),
               LocalizeString::get("client.module.reach.wallCheck.desc"), wallCheck);
    addSetting("targetLagRecords", LocalizeString::get("client.module.reach.targetLagRecords.name"),
               LocalizeString::get("client.module.reach.targetLagRecords.desc"), targetLagRecords);

    this->listen<ClickEvent>(&Reach::onClick, false, 20);
    this->listen<AfterMoveEvent>(&Reach::onAfterMove);
}

void Reach::onDisable() {
    pendingAttack = {};
}

Backtrack* Reach::resolveBacktrack() {
    if (!backtrackResolved) {
        auto mod = Necromancer::getModuleManager().find("Backtrack");
        backtrackModule = mod ? static_cast<Backtrack*>(mod.get()) : nullptr;
        backtrackResolved = true;
    }
    return backtrackModule;
}

AfterTrack* Reach::resolveAfterTrack() {
    if (!afterTrackResolved) {
        auto mod = Necromancer::getModuleManager().find("AfterTrack");
        afterTrackModule = mod ? static_cast<AfterTrack*>(mod.get()) : nullptr;
        afterTrackResolved = true;
    }
    return afterTrackModule;
}

bool Reach::pickTarget(SDK::LocalPlayer* lp, float maxRange, PendingAttack& out) {
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return false;

    auto level = ci->minecraft->getLevel();
    if (!level) return false;

    auto hit = level->getHitResult();
    if (!hit) return false;

    bool doWallCheck = std::get<BoolValue>(wallCheck);

    Vec3 direction = rayDirectionFromHit(hit, !doWallCheck);
    if (direction.magnitude() <= 0.0001f) return false;

    float nearest = maxRange;
    if (doWallCheck && hit->hitType == SDK::HitType::BLOCK) {
        float blockDist = hit->start.distance(hit->hitPos);
        if (blockDist > 0.001f) nearest = std::min(nearest, blockDist + 0.05f);
    }

    bool doPlayers = std::get<BoolValue>(players);
    bool doMobs = std::get<BoolValue>(mobs);
    bool doIgnoreFriends = std::get<BoolValue>(ignoreFriends);
    bool doLagRecords = std::get<BoolValue>(targetLagRecords);

    Backtrack* bt = doLagRecords ? resolveBacktrack() : nullptr;
    AfterTrack* at = doLagRecords ? resolveAfterTrack() : nullptr;

    SDK::BlockSource* region = doWallCheck ? ci->getRegion() : nullptr;
    if (region) WallCheck::beginPass();

    bool found = false;
    auto snap = EntityCache::get().snapshot();
    for (auto const& view : snap->views) {
        SDK::Actor* entt = view.actor;
        if (!entt || entt == lp || !entt->aabbShape) continue;
        if (view.isItem || !view.hasHealth || view.health <= 0.f) continue;
        if (view.invisible) continue;

        bool isPlayer = view.isPlayer;
        if (isPlayer ? !doPlayers : !doMobs) continue;
        if (doIgnoreFriends && isPlayer &&
            PlayerListManager::get().isFriend(reinterpret_cast<SDK::Player*>(entt)->playerName))
            continue;

        AABB liveBox = entt->getBoundingBox();
        auto liveDist = liveBox.intersectsRay(hit->start, direction, nearest, 0.08f);

        float actorNearest = liveDist ? *liveDist : nearest;
        TargetRecord actorRecord = TargetRecord::Live;
        AABB actorBox = liveBox;
        float actorRecordAge = -1.f;
        bool actorHit = liveDist.has_value();

        if (isPlayer && doLagRecords) {
            auto considerRecord = [&](AABB const& box, TargetRecord record, float recordAgeMs) {
                auto recordDist = box.intersectsRay(hit->start, direction, actorNearest, 0.f);
                if (!recordDist || *recordDist >= actorNearest) return;
                actorNearest = *recordDist;
                actorRecord = record;
                actorBox = box;
                actorRecordAge = recordAgeMs;
                actorHit = true;
            };

            std::vector<Backtrack::GhostRecord> backtrackRecords;
            if (bt && bt->getGhostRecords(view.runtimeId, backtrackRecords)) {
                for (auto const& record : backtrackRecords) {
                    considerRecord(record.box, TargetRecord::Backtrack, record.ageMs);
                }
            }

            AABB predicted {};
            if (at && at->getPredictedBox(view.runtimeId, predicted)) {
                considerRecord(predicted, TargetRecord::AfterTrack, -1.f);
            }
        }

        if (!actorHit || actorNearest >= nearest) continue;

        Vec3 impact { hit->start.x + direction.x * actorNearest, hit->start.y + direction.y * actorNearest,
                      hit->start.z + direction.z * actorNearest };

        if (region && !WallCheck::isVisible(region, hit->start, impact)) continue;

        nearest = actorNearest;
        out.runtimeID = view.runtimeId;
        out.record = actorRecord;
        out.box = actorBox;
        out.hitPoint = impact;
        out.recordAgeMs = actorRecordAge;
        found = true;
    }

    return found;
}

void Reach::onClick(Event& evG) {
    auto& ev = reinterpret_cast<ClickEvent&>(evG);
    if (ev.getClickType() != ClickEvent::ClickType::Left || !ev.isDown()) return;
    if (pendingAttack.active) return;

    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraftGame || !ci->minecraftGame->isCursorGrabbed()) return;
    if (Necromancer::get().getScreenManager().getActiveScreen()) return;

    auto lp = ci->getLocalPlayer();
    if (!lp || !lp->gameMode) return;

    float maxRange = std::get<FloatValue>(range).value;
    if (maxRange <= vanillaReach) return;

    PendingAttack candidate {};
    if (!pickTarget(lp, maxRange, candidate)) return;

    auto* target = EntityCache::get().findByRuntimeID(candidate.runtimeID);
    if (!target) return;

    if (candidate.record == TargetRecord::Live) {
        float distance = target->getBoundingBox().getCenter().distance(lp->getPos());
        if (distance <= vanillaReach) return;
    }

    candidate.active = true;
    pendingAttack = candidate;
}

void Reach::onAfterMove(Event&) {
    if (!pendingAttack.active) return;

    PendingAttack attack = pendingAttack;
    pendingAttack = {};

    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    auto* target = EntityCache::get().findByRuntimeID(attack.runtimeID);
    if (!lp || !lp->gameMode || !target) return;
    if (auto hp = target->getHealth(); !hp || *hp <= 0.f) return;

    if (attack.record == TargetRecord::Backtrack) {
        auto* bt = resolveBacktrack();
        if (!bt || !bt->queueGhostAttack(attack.runtimeID, attack.box, attack.hitPoint, attack.recordAgeMs)) return;
        TargetManager::setTarget(target);
        return;
    }

    if (!Signatures::GameMode_attack.result) return;
    if (auto* bt = resolveBacktrack()) bt->allowDirectAttack(attack.runtimeID);

    using GameModeAttackFn = __int64 (*)(void*, SDK::Actor*, char, Vec3*);
    Vec3 clickPos = attack.hitPoint;
    reinterpret_cast<GameModeAttackFn>(Signatures::GameMode_attack.result)(lp->gameMode, target, 0, &clickPos);
    TargetManager::setTarget(target);
}
