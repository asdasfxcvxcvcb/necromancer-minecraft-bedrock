#include "pch.h"
#include "Triggerbot.h"
#include "AfterTrack.h"
#include "Backtrack.h"
#include "client/misc/EntityCache.h"
#include "client/misc/MaceUtil.h"
#include "client/misc/TargetManager.h"
#include "client/misc/PlayerListManager.h"
#include "client/event/events/AfterMoveEvent.h"
#include <client/screen/ScreenManager.h>
#include <mc/common/client/game/ClientInstance.h>
#include <mc/common/client/game/MinecraftGame.h>
#include <mc/common/client/game/MouseDevice.h>
#include <mc/common/client/game/MouseAction.h>
#include <mc/common/client/player/LocalPlayer.h>
#include <mc/common/world/Minecraft.h>
#include <mc/common/world/level/Level.h>
#include <mc/common/world/level/HitResult.h>
#include <mc/common/world/actor/Actor.h>
#include <mc/common/world/actor/player/Player.h>
#include <mc/common/world/actor/player/PlayerInventory.h>
#include <mc/common/world/actor/player/Inventory.h>
#include <mc/common/world/actor/player/GameMode.h>
#include <mc/common/world/ItemStack.h>
#include <mc/common/world/WeaponItem.h>
#include <mc/common/world/DiggerItem.h>
#include <mc/common/entity/component/MoveInputComponent.h>
#include <mc/common/util/BasicPrintStream.h>
#include <mc/common/nbt/CompoundTag.h>

namespace {
    constexpr int enchIdProtection = 0;
    constexpr int enchIdSharpness = 9;

    int getEnchantLevel(SDK::ItemStack* stack, int enchId) {
        if (!stack || !stack->tag) return 0;

        SDK::BasicPrintStream ps {};
        stack->tag->print("", ps);
        std::string const& dump = ps.mStr;

        auto enchPos = dump.find("\"ench\"");
        if (enchPos == std::string::npos) return 0;

        auto parseNum = [&](size_t colonPos) {
            size_t i = colonPos + 1;
            while (i < dump.size() && (dump[i] == ' ' || dump[i] == '\t')) i++;
            bool neg = false;
            if (i < dump.size() && dump[i] == '-') {
                neg = true;
                i++;
            }
            int val = 0;
            bool any = false;
            while (i < dump.size() && dump[i] >= '0' && dump[i] <= '9') {
                val = val * 10 + (dump[i] - '0');
                i++;
                any = true;
            }
            return any ? (neg ? -val : val) : 0;
        };

        size_t searchFrom = enchPos;
        while (true) {
            auto idPos = dump.find("\"id\"", searchFrom);
            if (idPos == std::string::npos) return 0;
            auto idColon = dump.find(':', idPos);
            if (idColon == std::string::npos) return 0;
            int id = parseNum(idColon);

            auto lvlPos = dump.find("\"lvl\"", idColon);
            if (lvlPos == std::string::npos) return 0;
            auto lvlColon = dump.find(':', lvlPos);
            if (lvlColon == std::string::npos) return 0;
            int lvl = parseNum(lvlColon);

            if (id == enchId) return lvl;
            searchFrom = lvlColon;
        }
    }

    int getEnchantLevelCached(SDK::ItemStack* stack, int enchId) {
        if (!stack || !stack->tag) return 0;
        struct Entry {
            int prot = -1;
            int sharp = -1;
            std::chrono::steady_clock::time_point at {};
        };
        static std::unordered_map<SDK::ItemStack*, Entry> cache;
        if (cache.size() > 64) cache.clear();
        auto now = std::chrono::steady_clock::now();
        auto& e = cache[stack];
        if (now - e.at > 250ms) {
            e = Entry {};
            e.at = now;
        }
        int* slot = enchId == enchIdProtection ? &e.prot : &e.sharp;
        if (*slot < 0) *slot = getEnchantLevel(stack, enchId);
        return *slot;
    }

    float weaponBaseDamage(SDK::ItemStack* stack) {
        if (!stack) return 1.f;
        auto* item = stack->getItem();
        if (!item) return 1.f;
        std::string id = item->namespacedId.getString();

        static const std::pair<std::string_view, float> table[] = {
            { "minecraft:wooden_sword", 4.f },  { "minecraft:golden_sword", 4.f },
            { "minecraft:stone_sword", 5.f },   { "minecraft:iron_sword", 6.f },
            { "minecraft:diamond_sword", 7.f }, { "minecraft:netherite_sword", 8.f },
            { "minecraft:wooden_axe", 3.f },    { "minecraft:golden_axe", 3.f },
            { "minecraft:stone_axe", 4.f },     { "minecraft:iron_axe", 5.f },
            { "minecraft:diamond_axe", 6.f },   { "minecraft:netherite_axe", 7.f },
            { "minecraft:trident", 9.f },       { "minecraft:mace", 6.f },
        };
        for (auto& [name, dmg] : table) {
            if (id == name) return dmg;
        }

        if (id.ends_with("_sword") || id == "minecraft:mace") {
            if (auto* tier = static_cast<SDK::WeaponItem*>(item)->tier) {
                return 1.f + static_cast<float>(tier->damage);
            }
        }
        if (id.ends_with("_pickaxe") || id.ends_with("_axe") || id.ends_with("_shovel") || id.ends_with("_hoe")) {
            if (auto* tier = static_cast<SDK::DiggerItem*>(item)->tier) {
                return 1.f + static_cast<float>(tier->damage);
            }
        }
        return 1.f;
    }

    int armorPointsForPiece(std::string const& id, int slot) {
        auto pts = [slot](int h, int c, int l, int b) {
            switch (slot) {
            case 0: return h;
            case 1: return c;
            case 2: return l;
            default: return b;
            }
        };
        if (id.find("turtle_helmet") != std::string::npos) return slot == 0 ? 2 : 0;
        if (id.find("leather_") != std::string::npos) return pts(1, 3, 2, 1);
        if (id.find("golden_") != std::string::npos) return pts(2, 5, 3, 1);
        if (id.find("chainmail_") != std::string::npos) return pts(2, 5, 4, 1);
        if (id.find("iron_") != std::string::npos) return pts(2, 6, 5, 2);
        if (id.find("diamond_") != std::string::npos) return pts(3, 8, 6, 3);
        if (id.find("netherite_") != std::string::npos) return pts(3, 8, 6, 3);
        return 0;
    }
}

Triggerbot::Triggerbot()
    : Module("Triggerbot", LocalizeString::get("client.module.triggerbot.name"),
             LocalizeString::get("client.module.triggerbot.desc"), GAME, nokeybind) {
    addSetting("players", LocalizeString::get("client.module.triggerbot.players.name"),
               LocalizeString::get("client.module.triggerbot.players.desc"), players);
    addSetting("mobs", LocalizeString::get("client.module.triggerbot.mobs.name"),
               LocalizeString::get("client.module.triggerbot.mobs.desc"), mobs);
    addSetting("ignoreFriends", LocalizeString::get("client.module.triggerbot.ignoreFriends.name"),
               LocalizeString::get("client.module.triggerbot.ignoreFriends.desc"), ignoreFriends);
    addSetting("hitBehindWall", LocalizeString::get("client.module.triggerbot.hitBehindWall.name"),
               LocalizeString::get("client.module.triggerbot.hitBehindWall.desc"), hitBehindWall);

    auto cpsSet = addSliderSetting("cps", LocalizeString::get("client.module.triggerbot.cps.name"),
                                   LocalizeString::get("client.module.triggerbot.cps.desc"), cps, FloatValue(1.f),
                                   FloatValue(30.f), FloatValue(1.f));
    cpsSet->floatEditMax = 10000.f;

    addSliderSetting("range", LocalizeString::get("client.module.triggerbot.range.name"),
                     LocalizeString::get("client.module.triggerbot.range.desc"), range, FloatValue(1.f),
                     FloatValue(15.f), FloatValue(0.5f));

    addSetting("criticalOnly", LocalizeString::get("client.module.triggerbot.criticalOnly.name"),
               LocalizeString::get("client.module.triggerbot.criticalOnly.desc"), criticalOnly);
    addSetting("maceSmashOnly", LocalizeString::get("client.module.triggerbot.maceSmashOnly.name"),
               LocalizeString::get("client.module.triggerbot.maceSmashOnly.desc"), maceSmashOnly);
    addSetting("skipCritsIfKillable", LocalizeString::get("client.module.triggerbot.skipCritsIfKillable.name"),
               LocalizeString::get("client.module.triggerbot.skipCritsIfKillable.desc"), skipCritsIfKillable,
               "criticalOnly"_istrue);

    addSetting("backtrackTarget", LocalizeString::get("client.module.triggerbot.backtrackTarget.name"),
               LocalizeString::get("client.module.triggerbot.backtrackTarget.desc"), backtrackTarget, "players"_istrue);
    addSetting("noRepeatPoint", LocalizeString::get("client.module.triggerbot.noRepeatPoint.name"),
               LocalizeString::get("client.module.triggerbot.noRepeatPoint.desc"), noRepeatPoint,
               Setting::Condition(std::vector<Setting::SingleCond> {
                   { "players", { 1 }, false },
                   { "backtrackTarget", { 1 }, false },
               }));
    addSliderSetting("pointGap", LocalizeString::get("client.module.triggerbot.pointGap.name"),
                     LocalizeString::get("client.module.triggerbot.pointGap.desc"), pointGap, FloatValue(0.05f),
                     FloatValue(1.f), FloatValue(0.05f),
                     Setting::Condition(std::vector<Setting::SingleCond> {
                         { "players", { 1 }, false },
                         { "backtrackTarget", { 1 }, false },
                         { "noRepeatPoint", { 1 }, false },
                     }));

    this->listen<UpdateEvent>(&Triggerbot::onUpdate);
    this->listen<AfterMoveEvent>(&Triggerbot::onAfterMove);
}

void Triggerbot::afterLoadConfig() {
    auto& cpsVal = std::get<FloatValue>(cps).value;
    if (cpsVal < 1.f) cpsVal = 1.f;
}

void Triggerbot::onDisable() {
    nextAttack = std::chrono::steady_clock::now();
    criticalWaitUntil = {};
    criticalWaitActive = false;
    wasOnGround = true;
    hasLastHit = false;
    lastHitTarget = 0;
    pendingAttack = {};
}

Backtrack* Triggerbot::resolveBacktrack() {
    if (!backtrackResolved) {
        auto mod = Necromancer::getModuleManager().find("Backtrack");
        backtrackModule = mod ? static_cast<Backtrack*>(mod.get()) : nullptr;
        backtrackResolved = true;
    }
    return backtrackModule;
}

AfterTrack* Triggerbot::resolveAfterTrack() {
    if (!afterTrackResolved) {
        auto mod = Necromancer::getModuleManager().find("AfterTrack");
        afterTrackModule = mod ? static_cast<AfterTrack*>(mod.get()) : nullptr;
        afterTrackResolved = true;
    }
    return afterTrackModule;
}

bool Triggerbot::currentAimHeight(TargetSelection const& target, float& outY) {
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft || !target.actor) return false;
    auto level = ci->minecraft->getLevel();
    if (!level) return false;
    auto hit = level->getHitResult();
    if (!hit) return false;

    Vec3 toHit = hit->hitPos - hit->start;
    Vec3 direction = target.obstructed ? hit->end.normalized()
                                       : (toHit.magnitude() > 0.001f ? toHit.normalized() : hit->end.normalized());
    if (direction.magnitude() <= 0.0001f) return false;

    auto dist = target.box.intersectsRay(hit->start, direction, std::get<FloatValue>(range).value, 0.f);
    if (!dist) return false;

    outY = hit->start.y + direction.y * *dist;
    return true;
}

Triggerbot::TargetSelection Triggerbot::pickTarget(float maxRange) {
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return {};

    auto level = ci->minecraft->getLevel();
    auto lp = ci->getLocalPlayer();
    if (!level || !lp) return {};

    auto hit = level->getHitResult();
    if (!hit) return {};

    bool doHitBehindWall = std::get<BoolValue>(hitBehindWall);
    Vec3 toHit = hit->hitPos - hit->start;
    Vec3 direction = doHitBehindWall ? hit->end.normalized()
                                     : (toHit.magnitude() > 0.001f ? toHit.normalized() : hit->end.normalized());
    if (direction.magnitude() <= 0.0001f) return {};

    float wallDistance = std::numeric_limits<float>::infinity();
    float nearest = maxRange;
    if (hit->hitType == SDK::HitType::BLOCK) {
        float blockDist = hit->start.distance(hit->hitPos);
        if (blockDist > 0.001f) {
            wallDistance = blockDist;
            if (!doHitBehindWall) nearest = std::min(nearest, blockDist + 0.05f);
        }
    }

    bool doPlayers = std::get<BoolValue>(players);
    bool doMobs = std::get<BoolValue>(mobs);
    bool doIgnoreFriends = std::get<BoolValue>(ignoreFriends);
    bool doLagRecords = std::get<BoolValue>(backtrackTarget);
    Backtrack* bt = doLagRecords ? resolveBacktrack() : nullptr;
    AfterTrack* at = doLagRecords ? resolveAfterTrack() : nullptr;

    TargetSelection best;
    auto consider = [&](SDK::Actor* actor, AABB const& box, TargetRecord record, float recordAgeMs, float pad) {
        auto hitDist = box.intersectsRay(hit->start, direction, nearest, pad);
        if (!hitDist || *hitDist >= nearest) return;
        nearest = *hitDist;
        best.actor = actor;
        best.record = record;
        best.box = box;
        best.hitPoint = Vec3 { hit->start.x + direction.x * *hitDist, hit->start.y + direction.y * *hitDist,
                              hit->start.z + direction.z * *hitDist };
        best.recordAgeMs = recordAgeMs;
        best.obstructed = std::isfinite(wallDistance) && *hitDist > wallDistance + 0.05f;
    };

    auto snap = EntityCache::get().snapshot();
    for (auto const& view : snap->views) {
        SDK::Actor* entt = view.actor;
        if (!entt || entt == lp || !entt->aabbShape) continue;
        if (view.isItem || !view.hasHealth || view.health <= 0.f) continue;
        bool isPlayer = view.isPlayer;
        if (isPlayer ? !doPlayers : !doMobs) continue;
        if (view.invisible) continue;
        if (doIgnoreFriends && isPlayer &&
            PlayerListManager::get().isFriend(reinterpret_cast<SDK::Player*>(entt)->playerName))
            continue;

        AABB liveBox = entt->getBoundingBox();
        auto liveDist = liveBox.intersectsRay(hit->start, direction, nearest, 0.08f);
        if (liveDist && *liveDist < nearest) {
            consider(entt, liveBox, TargetRecord::Live, -1.f, 0.08f);
        }

        if (!isPlayer || !doLagRecords) continue;

        float actorNearest = liveDist ? *liveDist : nearest;
        TargetRecord actorRecord = liveDist ? TargetRecord::Live : TargetRecord::Backtrack;
        AABB actorBox = liveBox;
        float actorRecordAge = -1.f;
        bool actorHit = liveDist.has_value();

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

        AABB recordBox {};
        if (at && at->getPredictedBox(view.runtimeId, recordBox)) {
            considerRecord(recordBox, TargetRecord::AfterTrack, -1.f);
        }

        if (actorHit && actorRecord != TargetRecord::Live && actorNearest < nearest) {
            consider(entt, actorBox, actorRecord, actorRecordAge, 0.f);
        }
    }
    return best;
}

bool Triggerbot::performDirectAttack(SDK::LocalPlayer* lp, TargetSelection const& target) {
    if (!lp || !target.actor) return false;
    pendingAttack.runtimeID = target.actor->getRuntimeID();
    pendingAttack.record = target.record;
    pendingAttack.box = target.box;
    pendingAttack.hitPoint = target.hitPoint;
    pendingAttack.recordAgeMs = target.recordAgeMs;
    pendingAttack.obstructed = target.obstructed;
    pendingAttack.fireAt = std::chrono::steady_clock::now();
    pendingAttack.active = true;
    return true;
}

void Triggerbot::onAfterMove(Event&) {
    if (!pendingAttack.active) return;
    if (std::chrono::steady_clock::now() < pendingAttack.fireAt) return;

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

float estimateNormalDamage(SDK::Player* lp, SDK::Actor* target) {
    SDK::ItemStack* held = nullptr;
    if (lp->supplies && lp->supplies->inventory) {
        held = lp->supplies->inventory->getItem(lp->supplies->selectedSlot);
    }

    float raw = weaponBaseDamage(held) + 1.25f * static_cast<float>(getEnchantLevelCached(held, enchIdSharpness));

    int defense = 0;
    int epf = 0;
    for (int slot = 0; slot < 4; slot++) {
        auto* armor = target->getArmor(slot);
        if (!armor || !armor->getItem()) continue;
        defense += armorPointsForPiece(armor->getItem()->namespacedId.getString(), slot);
        epf += getEnchantLevelCached(armor, enchIdProtection);
    }

    float afterArmor = raw * (1.f - static_cast<float>(std::min(20, defense)) * 0.04f);
    return afterArmor * (1.f - static_cast<float>(std::min(20, epf)) * 0.04f);
}

bool Triggerbot::normalHitKills(SDK::Actor* target) {
    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (!lp) return false;

    auto hp = target->getHealth();
    if (!hp) return false;

    return estimateNormalDamage(lp, target) >= *hp;
}

bool Triggerbot::canFire(SDK::Actor* target) {
    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (!lp) return false;

    if (std::get<BoolValue>(maceSmashOnly) && MaceUtil::isHoldingMace(lp) && !MaceUtil::canSmash(lp)) {
        return false;
    }

    if (!std::get<BoolValue>(criticalOnly)) return true;

    bool onGround = lp->isOnGround();
    bool falling = lp->getVelocity().y < 0.f;

    if (!onGround && falling) return true;

    bool jumpedRecently = (std::chrono::steady_clock::now() - lastJumpTime) < 1s;
    if (!jumpedRecently && onGround) return true;

    if (std::get<BoolValue>(skipCritsIfKillable) && normalHitKills(target)) return true;

    return false;
}

void Triggerbot::onUpdate(Event&) {
    if (!std::get<BoolValue>(noRepeatPoint)) {
        hasLastHit = false;
        lastHitTarget = 0;
        lastHitY = 0.f;
    }

    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft || !ci->minecraftGame) return;
    if (!ci->minecraftGame->isCursorGrabbed()) return;
    if (Necromancer::get().getScreenManager().getActiveScreen()) return;

    auto lp = ci->getLocalPlayer();
    if (!lp || !lp->gameMode) return;

    auto now = std::chrono::steady_clock::now();

    bool onGround = lp->isOnGround();
    if (auto* move = lp->getMoveInputComponent()) {
        if (move->inputState.jumpInputWasPressed) lastJumpTime = now;
    }
    if (wasOnGround && !onGround) lastJumpTime = now;
    wasOnGround = onGround;

    bool criticalWindow = std::get<BoolValue>(criticalOnly) && !onGround && lp->getVelocity().y < 0.f;
    if (!criticalWindow) {
        criticalWaitActive = false;
        criticalWaitUntil = {};
    } else if (!criticalWaitActive) {
        criticalWaitActive = true;
        criticalWaitUntil = now + std::chrono::milliseconds(50);
    }

    auto target = pickTarget(std::get<FloatValue>(range).value);
    if (!target.actor || !canFire(target.actor)) {
        nextAttack = now;
        return;
    }

    if (nextAttack < now - std::chrono::milliseconds(100)) nextAttack = now;

    // No Repeat Point: hold fire until the crosshair has moved far enough vertically
    // from the last hit on this target. Some servers drop a second hit in a band they
    // already accepted one in, so firing again there wastes the click entirely.
    //
    // Only applies when this target is actually being hit as a ghost. The live model has
    // no such restriction, so gating it there would throttle normal combat -- including
    // mobs and any player with no lag record yet.
    float aimY = 0.f;
    bool haveAimY = false;
    bool gateOnHeight = target.record != TargetRecord::Live && std::get<BoolValue>(noRepeatPoint);
    if (gateOnHeight) {
        haveAimY = currentAimHeight(target, aimY);
        if (!haveAimY) {
            nextAttack = now;
            return;
        }
        uint64_t rid = target.actor->getRuntimeID();
        if (hasLastHit && lastHitTarget == rid) {
            float gap = std::clamp(std::get<FloatValue>(pointGap).value, 0.05f, 1.f);
            if (std::abs(aimY - lastHitY) < gap) return;
        }
    }

    bool useMouse = target.record == TargetRecord::Backtrack ||
                    (target.record == TargetRecord::Live && !target.obstructed);
    bool supplementDirect = target.record == TargetRecord::Live && target.obstructed;
    auto mouse = (useMouse || supplementDirect) ? SDK::MouseDevice::get() : nullptr;
    if ((useMouse || supplementDirect) && !mouse) {
        nextAttack = now;
        return;
    }

    auto pushClick = [mouse](bool down) {
        SDK::MouseAction action {};
        action.x = mouse->x;
        action.y = mouse->y;
        action.dx = 0;
        action.dy = 0;
        action.action = int8_t { 1 };
        action.data = down ? int8_t { 1 } : int8_t { 0 };
        action.pointerId = 0;
        action.forceMotionlessPointer = false;
        mouse->inputs.push_back(action);
    };

    if (now < nextAttack || pendingAttack.active) return;
    if (criticalWindow && now < criticalWaitUntil) return;

    if (useMouse) {
        if (target.record == TargetRecord::Backtrack) {
            auto* bt = resolveBacktrack();
            if (!bt || !bt->prepareGhostAttack(target.actor->getRuntimeID(), target.box, target.hitPoint,
                                               target.recordAgeMs)) {
                nextAttack = now;
                return;
            }
        } else {
            if (auto* bt = resolveBacktrack()) bt->allowDirectAttack(target.actor->getRuntimeID());
        }
        pushClick(true);
        pushClick(false);
        TargetManager::setTarget(target.actor);
    } else if (supplementDirect) {
        if (auto* bt = resolveBacktrack()) bt->allowDirectAttack(target.actor->getRuntimeID());
        pushClick(true);
        pushClick(false);
        if (!performDirectAttack(lp, target)) {
            nextAttack = now;
            return;
        }
        pendingAttack.fireAt = now + std::chrono::milliseconds(60);
    } else if (!performDirectAttack(lp, target)) {
        nextAttack = now;
        return;
    }

    if (gateOnHeight && haveAimY) {
        lastHitTarget = target.actor->getRuntimeID();
        lastHitY = aimY;
        hasLastHit = true;
    }

    float cpsVal = std::max(std::get<FloatValue>(cps).value, 1.f);
    nextAttack = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                           std::chrono::duration<float>(1.f / cpsVal));
}
