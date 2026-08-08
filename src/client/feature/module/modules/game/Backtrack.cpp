#include "pch.h"
#include "Backtrack.h"
#include "client/event/events/UpdateEvent.h"
#include "client/event/events/AfterMoveEvent.h"
#include "client/event/events/SendPacketEvent.h"
#include "client/event/events/PacketReceiveEvent.h"
#include "client/event/events/AttackEvent.h"
#include "client/event/events/ClickEvent.h"
#include "client/event/events/LeaveGameEvent.h"
#include "client/event/events/RenderLevelEvent.h"
#include "client/feature/module/modules/visual/AntiObs.h"
#include "client/Necromancer.h"
#include "client/misc/ClientMessageQueue.h"
#include "client/misc/EntityCache.h"
#include "client/misc/LatencySpoof.h"
#include "client/screen/ScreenManager.h"
#include "mc/Addresses.h"
#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/game/MinecraftGame.h"
#include "mc/common/client/player/LocalPlayer.h"
#include "mc/common/world/Minecraft.h"
#include "mc/common/world/level/Level.h"
#include "mc/common/world/level/HitResult.h"
#include "mc/common/world/level/BlockSource.h"
#include "mc/common/world/level/block/Block.h"
#include "mc/common/world/level/block/BlockLegacy.h"
#include "mc/common/world/actor/Actor.h"
#include "mc/common/world/actor/player/GameMode.h"
#include "mc/common/network/RakNetConnector.h"
#include "mc/common/network/MinecraftPackets.h"
#include "mc/common/network/PacketSender.h"
#include <util/DrawUtil3D.h>
#include <util/Logger.h>
#include <cstring>
#include <unordered_set>

#define BT_LOG(...) do { } while (0)

namespace {
    // A ray/box intersection returns a point exactly ON the box surface, and the
    // server's rewound box does not line up with ours perfectly -- so a point near an
    // edge falls outside it and the hit is dropped.
    //
    // Measured from a session log: with a 1.80-tall box, every click 0.39+ below the
    // top registered damage and every click 0.35 or less below the top did not. So the
    // vertical mismatch is ~0.37 and the top of the box is effectively unusable. The
    // horizontal faces show no such effect, hence separate insets.
    constexpr float clickInsetXZ = 0.12f;
    constexpr float clickInsetTop = 0.45f;     // measured dead band 0.37 + margin
    constexpr float clickInsetBottom = 0.12f;  // bottom lands fine, only trim the edge

    Vec3 pushInsideBox(Vec3 p, AABB box) {
        auto squeeze = [](float v, float lo, float hi, float loInset, float hiInset) {
            float a = lo + loInset;
            float b = hi - hiInset;
            if (a > b) return (lo + hi) * 0.5f;  // box too thin for the inset
            return std::clamp(v, a, b);
        };
        p.x = squeeze(p.x, box.lower.x, box.higher.x, clickInsetXZ, clickInsetXZ);
        p.y = squeeze(p.y, box.lower.y, box.higher.y, clickInsetBottom, clickInsetTop);
        p.z = squeeze(p.z, box.lower.z, box.higher.z, clickInsetXZ, clickInsetXZ);
        return p;
    }

    // Blocks a player cannot stand on. Everything absent from this list counts as
    // support, so an unrecognised block keeps the record valid rather than silently
    // making an enemy untargetable -- the failure that matters here is a false
    // "airborne", not a false "grounded".
    bool cannotSupport(std::string const& id) {
        static const std::unordered_set<std::string> noCollision = {
            "air", "cave_air", "void_air", "structure_void", "light_block", "water", "flowing_water", "lava",
            "flowing_lava", "tallgrass", "short_grass", "fern", "large_fern", "double_plant", "seagrass", "kelp",
            "vine", "torch", "redstone_torch", "unlit_redstone_torch", "soul_torch", "redstone_wire", "rail",
            "golden_rail", "detector_rail", "activator_rail", "tripwire", "web", "cobweb", "fire", "soul_fire",
            "sapling", "deadbush", "red_flower", "yellow_flower", "wheat", "carrots", "potatoes", "beetroot",
            "reeds", "sugar_cane", "nether_sprouts", "wither_rose", "crimson_roots", "warped_roots",
            "hanging_roots", "glow_lichen", "sculk_vein", "flower_pot", "banner", "item_frame", "string",
            "bamboo_sapling", "lever", "button", "sign", "standing_sign", "wall_sign",
        };
        return noCollision.contains(id);
    }

    // Keyed on BlockLegacy*, which is one shared instance per block type, so the
    // std::string that getString() returns by value is built once per type for the whole
    // session instead of once per probe.
    bool legacySupports(SDK::BlockLegacy* legacy) {
        thread_local std::unordered_map<SDK::BlockLegacy*, bool> verdict;
        auto it = verdict.find(legacy);
        if (it != verdict.end()) return it->second;
        bool supports = !cannotSupport(legacy->name.getString());
        verdict.emplace(legacy, supports);
        return supports;
    }

    bool supportsStandingAt(SDK::BlockSource* region, int x, int y, int z) {
        auto* block = region->getBlock(BlockPos { x, y, z });
        if (!block) return false;
        auto* legacy = block->legacyBlock;
        if (!legacy) return region->isSolidBlockingBlockAt(x, y, z);
        return legacySupports(legacy);
    }

    // True when something holds this box up. Derived from the world rather than from
    // Actor::isOnGround(), because that is a presence test for OnGroundFlagComponent --
    // a flag the client's own movement code maintains for the local player. For a remote
    // player the client runs no such physics, so it cannot be relied on there.
    //
    // Probes just under the feet: the cell containing the feet holds whatever the player
    // is standing *in*, the cell below holds what they stand *on*.
    bool boxIsSupported(SDK::BlockSource* region, AABB const& box) {
        if (!region) return true;  // no world to judge with; treat as grounded

        // Remote positions arrive quantised and interpolated, so a standing player often
        // sits a hair above the surface. Without this margin they would read airborne
        // while stood perfectly still. A jump clears 0.42 in its first tick, far outside
        // it, so real airtime is still caught.
        constexpr float probeDepth = 0.08f;
        constexpr float edgeInset = 0.001f;

        int y = static_cast<int>(std::floor(box.lower.y - probeDepth));

        // Centre first: a player is normally stood over the middle of a block, so this
        // usually answers it in one lookup. The corners only matter for someone perched
        // on an edge, where the centre hangs over air.
        float cx = (box.lower.x + box.higher.x) * 0.5f;
        float cz = (box.lower.z + box.higher.z) * 0.5f;
        if (supportsStandingAt(region, static_cast<int>(std::floor(cx)), y, static_cast<int>(std::floor(cz))))
            return true;

        int x0 = static_cast<int>(std::floor(box.lower.x + edgeInset));
        int x1 = static_cast<int>(std::floor(box.higher.x - edgeInset));
        int z0 = static_cast<int>(std::floor(box.lower.z + edgeInset));
        int z1 = static_cast<int>(std::floor(box.higher.z - edgeInset));
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z) {
                if (x == static_cast<int>(std::floor(cx)) && z == static_cast<int>(std::floor(cz))) continue;
                if (supportsStandingAt(region, x, y, z)) return true;
            }
        }
        return false;
    }
}

Backtrack::Backtrack()
    : Module("Backtrack", LocalizeString::get("client.module.backtrack.name"),
             LocalizeString::get("client.module.backtrack.desc"), GAME, nokeybind) {
    // 50ms steps: a server tick. Records are only stored on tick boundaries, so a value
    // between them just resolves to the same record as the boundary below it.
    auto timeSet = addSliderSetting("time", LocalizeString::get("client.module.backtrack.time.name"),
                     LocalizeString::get("client.module.backtrack.time.desc"), timeMs, FloatValue(0.f),
                     FloatValue(2000.f), FloatValue(recordStepMs));
    auto fakeLatSet = addSliderSetting("fakeLatency", LocalizeString::get("client.module.backtrack.fakeLatency.name"),
                     LocalizeString::get("client.module.backtrack.fakeLatency.desc"), fakeLatencyMs, FloatValue(0.f),
                     FloatValue(2000.f), FloatValue(recordStepMs));
    auto stackSet = addSliderSetting("stackLatencyDelay", LocalizeString::get("client.module.backtrack.stackLatencyDelay.name"),
                     LocalizeString::get("client.module.backtrack.stackLatencyDelay.desc"), stackLatencyDelayMs, FloatValue(0.f),
                     FloatValue(2000.f), FloatValue(recordStepMs));
    // Sliders stop at 2000 because that is the useful range, but a typed value is
    // honoured all the way to the buffer's real ceiling.
    timeSet->floatEditMax = static_cast<float>(maxRecordMs);
    fakeLatSet->floatEditMax = static_cast<float>(maxLatencyMs);
    stackSet->floatEditMax = static_cast<float>(maxLatencyMs);

    addSetting("onlyLastRecord", LocalizeString::get("client.module.backtrack.onlyLastRecord.name"),
               LocalizeString::get("client.module.backtrack.onlyLastRecord.desc"), onlyLastRecord);
    addSetting("freezeBacktrack", LocalizeString::get("client.module.backtrack.freezeBacktrack.name"),
               LocalizeString::get("client.module.backtrack.freezeBacktrack.desc"), freezeBacktrack);
    addSetting("invalidateAirborne", LocalizeString::get("client.module.backtrack.invalidateAirborne.name"),
               LocalizeString::get("client.module.backtrack.invalidateAirborne.desc"), invalidateAirborne);
    addSliderSetting("ghostCount", LocalizeString::get("client.module.backtrack.ghostCount.name"),
                     LocalizeString::get("client.module.backtrack.ghostCount.desc"), ghostCount, FloatValue(1.f),
                     FloatValue(12.f), FloatValue(1.f), "onlyLastRecord"_isfalse);
    addSetting("hitbox", LocalizeString::get("client.module.backtrack.hitbox.name"),
               LocalizeString::get("client.module.backtrack.hitbox.desc"), hitbox);
    addSetting("hitboxColor", LocalizeString::get("client.module.backtrack.hitboxColor.name"),
               LocalizeString::get("client.module.backtrack.hitboxColor.desc"), hitboxColor, "hitbox"_istrue);
    hitboxStyle.addEntry(EnumEntry(style_outline, LocalizeString::get("client.module.backtrack.hitboxStyle.outline.name"),
                                   LocalizeString::get("client.module.backtrack.hitboxStyle.outline.desc")));
    hitboxStyle.addEntry(EnumEntry(style_filled, LocalizeString::get("client.module.backtrack.hitboxStyle.filled.name"),
                                   LocalizeString::get("client.module.backtrack.hitboxStyle.filled.desc")));
    hitboxStyle.addEntry(EnumEntry(style_both, LocalizeString::get("client.module.backtrack.hitboxStyle.both.name"),
                                   LocalizeString::get("client.module.backtrack.hitboxStyle.both.desc")));
    addEnumSetting("hitboxStyle", LocalizeString::get("client.module.backtrack.hitboxStyle.name"),
                   LocalizeString::get("client.module.backtrack.hitboxStyle.desc"), hitboxStyle, "hitbox"_istrue);
    addSetting("throughWalls", LocalizeString::get("client.module.backtrack.throughWalls.name"),
               LocalizeString::get("client.module.backtrack.throughWalls.desc"), throughWalls, "hitbox"_istrue);

    this->listen<UpdateEvent>(&Backtrack::onUpdate);
    this->listen<AfterMoveEvent>(&Backtrack::onAfterMove);
    this->listen<SendPacketEvent>(&Backtrack::onSendPacket);
    this->listen<AttackEvent>(&Backtrack::onAttack);
    this->listen<ClickEvent>(&Backtrack::onClick);
    this->listen<LeaveGameEvent>(&Backtrack::onLeaveGame);
    Eventing::get().listen<RenderLevelEvent, &Backtrack::onRenderLevel>(this);
}

void Backtrack::clearState() {
    buffers.clear();
    attackQueue.clear();
    appliedLatencyMs = 0;
    LatencySpoof::setLatency(0);
    pendingRID = 0;
    swallowRID = 0;
    reissueRID = 0;
    hasClickPending = false;
    freezeActive = false;
    aimedGhostAge = -1.f;
    pendingReportedOffset = -1.f;
    confirmQueue.clear();
}

void Backtrack::onEnable() {
    clearState();
    BT_LOG("[BT] ==== enabled: time={:.0f} fakeLat={:.0f} stackOff={:.0f} ghosts={:.0f} onlyLast={} freeze={} "
           "attackSig=0x{:X} ====",
           std::get<FloatValue>(timeMs).value, std::get<FloatValue>(fakeLatencyMs).value,
           std::get<FloatValue>(stackLatencyDelayMs).value, std::get<FloatValue>(ghostCount).value,
           std::get<BoolValue>(onlyLastRecord) ? 1 : 0, std::get<BoolValue>(freezeBacktrack) ? 1 : 0,
           Signatures::GameMode_attack.result);
}

void Backtrack::onDisable() {
    clearState();
}

void Backtrack::onLeaveGame(Event&) {
    clearState();
}

void Backtrack::onUpdate(Event&) {
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return;
    auto level = ci->minecraft->getLevel();
    auto lp = ci->getLocalPlayer();
    if (!level || !lp) return;

    // Latch the freeze instant on the rising edge. Sampling keeps running while
    // frozen so the buffer still has history when it is released.
    bool wantFreeze = std::get<BoolValue>(freezeBacktrack);
    if (wantFreeze && !freezeActive) {
        freezeAt = std::chrono::steady_clock::now();
        freezeActive = true;
    } else if (!wantFreeze && freezeActive) {
        freezeActive = false;
    }

    samplePositions();
    applyFakeLatency();
    processAirClick();
    processConfirmations();
}

void Backtrack::onAfterMove(Event&) {
    processAttackQueue();
}

void Backtrack::samplePositions() {
    auto now = std::chrono::steady_clock::now();
    auto ci = SDK::ClientInstance::get();
    auto lp = ci->getLocalPlayer();
    // One region lookup for the whole pass; the ground probe needs it per player.
    auto* region = ci->getRegion();
    bool checkAirborne = std::get<BoolValue>(invalidateAirborne);

    seenScratch.clear();
    auto snap = EntityCache::get().snapshot();
    for (auto* actor : snap->actors) {
        if (!actor || actor == lp || !actor->isPlayer()) continue;
        uint64_t rid = actor->getRuntimeID();
        seenScratch.insert(rid);
        auto& buf = buffers[rid];
        if (buf.empty() || now - buf.back().at >= std::chrono::milliseconds(20)) {
            AABB liveBox = actor->getBoundingBox();
            // Only probe the world when something actually consumes the answer. With the
            // setting off this is a plain store and costs nothing per player.
            bool rawAirborne = false;
            bool airborne = false;
            if (checkAirborne) {
                // Judged from the world, not from Actor::isOnGround(). That call tests for
                // OnGroundFlagComponent, which the client's movement code maintains for the
                // player it simulates -- us. A remote player's physics runs on the server,
                // so the client does not own that flag for them, and it was reading
                // airborne for enemies stood perfectly still. Invalidate Airborne Records
                // then rejected every record they had, which is why grounded players stayed
                // untargetable no matter what the blend or the debounce did.
                rawAirborne = !boxIsSupported(region, liveBox);
                // A real jump stays off the ground for many consecutive samples; one lone
                // sample of air is a quantisation artefact. Requiring two in a row costs
                // 20ms of detection at takeoff and drops the false positives.
                airborne = rawAirborne && !buf.empty() && buf.back().rawAirborne;
            }
            buf.push_back({ now, actor->getPos(), liveBox, airborne, rawAirborne });
        }

        // Retention has to cover the whole slider range plus a little slack, or a
        // request past the buffer's reach finds no sample and the player silently
        // gets no ghost box. Sized from the slider max, not a fixed guess.
        while (!buf.empty() && now - buf.front().at > std::chrono::milliseconds(maxRecordMs + 200))
            buf.pop_front();
        while (buf.size() > maxSamples) buf.pop_front();
    }

    for (auto it = buffers.begin(); it != buffers.end();) {
        if (!seenScratch.contains(it->first))
            it = buffers.erase(it);
        else
            ++it;
    }
}

void Backtrack::processAirClick() {
    if (!hasClickPending) return;
    auto now = std::chrono::steady_clock::now();
    if (now - clickPendingAt < std::chrono::milliseconds(60)) return;
    hasClickPending = false;
    if (lastAttackEventAt >= clickPendingAt) {
        BT_LOG("[BT] airclick SKIP: vanilla AttackEvent already fired for this click");
        return;
    }

    Vec3 aimedGhost {};
    AABB aimedBox {};
    Vec3 aimedHit {};
    auto* target = pickStaleTarget(legitReach, &aimedGhost, &aimedBox, &aimedHit);
    if (!target) {
        BT_LOG("[BT] airclick MISS: no ghost under crosshair (ages={} onlyLast={})", ghostAgeScratch.size(),
               std::get<BoolValue>(onlyLastRecord) ? 1 : 0);
        return;
    }

    // pickStaleTarget just recorded which ghost the ray hit; report a timestamp that
    // matches its age so the server rewinds to that ghost and not the slider's.
    pendingReportedOffset = adjustedOffsetFor(aimedGhostAge);
    reportedOffsetUntil = now + std::chrono::milliseconds(reportedOffsetHoldMs);

    BT_LOG("[BT] airclick HIT rid={} ghostAge={:.0f}ms offset={:.0f} hit=({:.2f},{:.2f},{:.2f}) "
           "boxY=[{:.2f}..{:.2f}] half={}",
           target->getRuntimeID(), aimedGhostAge, pendingReportedOffset, aimedHit.x, aimedHit.y, aimedHit.z,
           aimedBox.lower.y, aimedBox.higher.y,
           aimedHit.y > (aimedBox.lower.y + aimedBox.higher.y) * 0.5f ? "UPPER" : "LOWER");

    attackQueue.push_back({ target->getRuntimeID(), clickPendingAt, aimedGhost, aimedBox, aimedHit,
                            pendingReportedOffset });
    while (attackQueue.size() > 16) attackQueue.pop_front();
}

void Backtrack::processAttackQueue() {
    if (attackQueue.empty()) return;

    auto now = std::chrono::steady_clock::now();
    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (!lp || !lp->gameMode) {
        attackQueue.clear();
        return;
    }

    while (!attackQueue.empty() && attackQueue.front().fireAt <= now) {
        QueuedAttack attackFront = attackQueue.front();
        uint64_t rid = attackFront.runtimeID;
        attackQueue.pop_front();

        auto* target = resolveActor(rid);
        if (!target) {
            BT_LOG("[BT] fire SKIP rid={}: target no longer resolvable", rid);
            continue;
        }

        // Use the ghost position captured when you actually aimed, not a fresh
        // lookup. By the time the delay elapses the buffer has rolled forward, so
        // re-querying it would build the attack from a different box than the one
        // under your crosshair at click time.
        Vec3 clickPos = attackFront.hitPoint;

        reissueRID = rid;
        reissueUntil = now + std::chrono::milliseconds(250);
        swallowRID = 0;

        if (!Signatures::GameMode_attack.result) {
            BT_LOG("[BT] fire ABORT: GameMode_attack signature unresolved");
            continue;
        }

        Vec3 livePos = target->getPos();
        auto hpBefore = target->getHealth();
        float hpBeforeVal = hpBefore ? *hpBefore : -1.f;

        // Announce the latency this ghost implies immediately before the attack, so the
        // server's rewind window is set to that instant when it validates the hit.
        // Waiting for the game's own echo did not work: it only replies on request, so
        // across dozens of hits we sent none at all.
        if (attackFront.reportedOffset > 0.f) sendLatencyProbe(attackFront.reportedOffset);

        using GameModeAttackFn = __int64 (*)(void*, SDK::Actor*, char, Vec3*);
        reinterpret_cast<GameModeAttackFn>(Signatures::GameMode_attack.result)(lp->gameMode, target, 0, &clickPos);

        // Distance between where we claim to hit and where the enemy actually is now.
        // A large value is expected (that is the whole point) but it is the number the
        // server's reach check sees, so it is worth having in the log.
        float sep = clickPos.distance(livePos);
        bool upper = clickPos.y > (attackFront.ghostBox.lower.y + attackFront.ghostBox.higher.y) * 0.5f;
        float belowTop = attackFront.ghostBox.higher.y - clickPos.y;

        // The game derives its attack direction from |clickPos - playerPos| (see the
        // lambda at 0x142680360), so that distance -- not the staleness one -- is what
        // a reach check would read. Logging both, plus the vertical delta, because that
        // is the only thing that changes between an upper and a lower aim.
        Vec3 selfPos = lp->getPos();
        float reachDist = clickPos.distance(selfPos);
        float dy = clickPos.y - selfPos.y;

        BT_LOG("[BT] fire rid={} half={} click=({:.2f},{:.2f},{:.2f}) live=({:.2f},{:.2f},{:.2f}) "
               "self=({:.2f},{:.2f},{:.2f}) reach={:.2f} dy={:+.2f} sep={:.2f} hpBefore={:.1f} offset={:.0f}",
               rid, upper ? "UPPER" : "LOWER", clickPos.x, clickPos.y, clickPos.z, livePos.x, livePos.y, livePos.z,
               selfPos.x, selfPos.y, selfPos.z, reachDist, dy, sep, hpBeforeVal, pendingReportedOffset);
        (void)reachDist;
        (void)dy;
        (void)sep;
        (void)upper;

        confirmQueue.push_back({ rid, hpBeforeVal, aimedGhostAge, pendingReportedOffset, belowTop, now, false });
        // One outstanding confirm per target. Several in flight all matched the same
        // health drop, so a single landed hit reported as three or four -- which made
        // the hit rate look far better than it was.
        for (auto& c : confirmQueue) {
            if (c.runtimeID == rid && !c.done && &c != &confirmQueue.back()) c.done = true;
        }
        while (confirmQueue.size() > 16) confirmQueue.pop_front();
    }
}

void Backtrack::processConfirmations() {
    if (confirmQueue.empty()) return;
    auto now = std::chrono::steady_clock::now();

    for (auto& c : confirmQueue) {
        if (c.done) continue;
        auto* actor = resolveActor(c.runtimeID);
        if (actor) {
            auto hp = actor->getHealth();
            if (hp && c.hpAtFire >= 0.f && *hp < c.hpAtFire) {
                BT_LOG("[BT] CONFIRM rid={} belowTop={:.2f} ghostAge={:.0f} DAMAGE {:.1f}->{:.1f} after {}ms",
                       c.runtimeID, c.belowTop, c.ghostAge, c.hpAtFire, *hp,
                       std::chrono::duration_cast<std::chrono::milliseconds>(now - c.firedAt).count());
                float landed = *hp;
                for (auto& other : confirmQueue) {
                    if (!other.done && other.runtimeID == c.runtimeID && other.hpAtFire >= landed) other.done = true;
                }
                continue;
            }
        }
        if (now - c.firedAt >= std::chrono::milliseconds(confirmWindowMs)) {
            BT_LOG("[BT] NODAMAGE rid={} belowTop={:.2f} ghostAge={:.0f} hp stayed {:.1f} after {}ms", c.runtimeID,
                   c.belowTop, c.ghostAge, c.hpAtFire, confirmWindowMs);
            c.done = true;
        }
    }

    // Compact from anywhere, not just the front: a single stuck entry at the head would
    // otherwise pin everything behind it and keep it all in the per-tick scan.
    std::erase_if(confirmQueue, [](PendingConfirm const& c) { return c.done; });
}

void Backtrack::onAttack(Event& evG) {
    auto& ev = reinterpret_cast<AttackEvent&>(evG);
    auto* target = ev.getActor();
    if (!target) return;
    auto now = std::chrono::steady_clock::now();
    uint64_t rid = target->getRuntimeID();
    if (rid == reissueRID && now < reissueUntil) {
        BT_LOG("[BT] attackEvent rid={} IGNORED (our own re-issue)", rid);
        return;
    }
    BT_LOG("[BT] attackEvent rid={} accepted -> pending", rid);
    lastAttackEventAt = now;
    pendingRID = rid;
    pendingAt = now;
}

void Backtrack::onClick(Event& evG) {
    auto& ev = reinterpret_cast<ClickEvent&>(evG);
    if (ev.getClickType() != ClickEvent::ClickType::Left || !ev.isDown()) return;

    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraftGame || !ci->minecraftGame->isCursorGrabbed()) return;
    if (Necromancer::get().getScreenManager().getActiveScreen()) return;
    if (!ci->getLocalPlayer()) return;

    hasClickPending = true;
    clickPendingAt = std::chrono::steady_clock::now();
}

bool Backtrack::cameraRay(Vec3& outEye, Vec3& outDir) {
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return false;
    auto level = ci->minecraft->getLevel();
    auto hit = level ? level->getHitResult() : nullptr;
    if (!hit) return false;

    // hit->end is a DIRECTION vector, not a world-space endpoint. Computing
    // (end - start) produced a ray aimed back toward the world origin instead of
    // where the camera looks. Use the same formula Triggerbot uses: prefer the
    // vector to the actual hit point, falling back to end as a raw direction.
    Vec3 toHit = hit->hitPos - hit->start;
    Vec3 dir = toHit.magnitude() > 0.001f ? toHit.normalized() : hit->end.normalized();
    if (dir.magnitude() <= 0.0001f) return false;

    outEye = hit->start;
    outDir = dir;
    return true;
}

SDK::Actor* Backtrack::pickStaleTarget(float maxDist, Vec3* outGhostPos, AABB* outGhostBox, Vec3* outHitPoint) {
    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (!lp) {
        return nullptr;
    }

    Vec3 eye {};
    Vec3 dir {};
    if (!cameraRay(eye, dir)) {
        return nullptr;
    }

    bool onlyLast = std::get<BoolValue>(onlyLastRecord);
    bool invalidAir = std::get<BoolValue>(invalidateAirborne);

    float best = maxDist;
    SDK::Actor* bestActor = nullptr;
    Vec3 bestGhost {};
    AABB bestBox {};
    Vec3 bestHit {};
    float bestAge = -1.f;
    auto snap = EntityCache::get().snapshot();
    for (auto* actor : snap->actors) {
        if (!actor || actor == lp || !actor->isPlayer()) continue;
        auto hp = actor->getHealth();
        if (!hp || *hp <= 0.f) continue;

        uint64_t rid = actor->getRuntimeID();
        // Per target: New Record Per Hit shifts each player's window independently.
        buildGhostAges(ghostAgeScratch, rid);
        // Test every ghost we draw, not just the one at the full window, so any of
        // the in-between boxes is aimable.
        for (float age : ghostAgeScratch) {
            bool fresh = false;
            Sample blended {};
            const Sample* sample = nullptr;
            // Interpolation first when Only Last Record is off: findSample returns the
            // nearest *older* stored record, so preferring it would collapse every
            // in-between ghost onto a recorded one.
            if (!onlyLast && interpolateSample(rid, age, blended)) {
                sample = &blended;
            } else {
                sample = findSample(rid, age, fresh);
            }
            if (!sample) continue;
            // Still drawn, just not targetable.
            if (invalidAir && sample->airborne) continue;

            // No expansion: padding the ghost box hands out free reach on top of the
            // distance limit, so the box has to be hit exactly as it was.
            auto hitDist = sample->box.intersectsRay(eye, dir, maxDist, 0.f);
            if (!hitDist) continue;
            if (*hitDist < best) {
                best = *hitDist;
                bestActor = actor;
                bestGhost = sample->pos;
                bestBox = sample->box;
                bestAge = age;
                // Exact crossing point along the ray, so the attack can report the
                // half of the box you actually aimed at.
                bestHit = pushInsideBox(
                    Vec3 { eye.x + dir.x * *hitDist, eye.y + dir.y * *hitDist, eye.z + dir.z * *hitDist },
                    sample->box);
            }
        }
    }

    if (bestActor && outGhostPos) *outGhostPos = bestGhost;
    if (bestActor && outGhostBox) *outGhostBox = bestBox;
    if (bestActor && outHitPoint) *outHitPoint = bestHit;
    aimedGhostAge = bestActor ? bestAge : -1.f;

    return bestActor;
}

float Backtrack::ghostAgeMs() {
    float base = std::clamp(std::get<FloatValue>(timeMs).value, 0.f, static_cast<float>(maxRecordMs));
    float lag = std::clamp(std::get<FloatValue>(fakeLatencyMs).value, 0.f, static_cast<float>(maxLatencyMs));
    float stack = std::clamp(std::get<FloatValue>(stackLatencyDelayMs).value, 0.f, static_cast<float>(maxLatencyMs));
    return std::clamp(base + lag + stack, 0.f, static_cast<float>(maxRecordMs));
}

float Backtrack::ghostAgeMsFor(uint64_t runtimeID) {
    return ghostAgeMs();
}

void Backtrack::applyFakeLatency() {
    // The delay itself lives at the RakNet socket layer (LatencySpoof). Holding
    // packet objects here was tried first and did nothing: the server derives our
    // latency from datagram timing below the packet layer, so nothing above it can
    // move that number.
    uint32_t want = 0;
    if (isEnabled()) {
        want = static_cast<uint32_t>(
            std::clamp(std::get<FloatValue>(fakeLatencyMs).value, 0.f, static_cast<float>(maxLatencyMs)));
    }
    if (want == appliedLatencyMs) return;
    appliedLatencyMs = want;
    LatencySpoof::setLatency(want);
}

void Backtrack::onSendPacket(Event& evG) {
    auto& ev = reinterpret_cast<SendPacketEvent&>(evG);
    auto* packet = ev.getPacket();
    if (!packet) return;

    if (packet->getID() == SDK::PacketID::NETWORK_STACK_LATENCY) {
        // Never touch our own probe: sendLatencyProbe already set the timestamp it
        // wants, and rewriting it here would double-apply the offset.
        if (probeInFlight) return;
        // A recent hit overrides the slider: its ghost sits at an age the slider was
        // never set to, and the server rewinds by whatever our timestamp implies.
        // The override survives until it has actually been reported at least once --
        // these echoes are periodic, not tied to our attacks, so clearing it when the
        // attack queue drained meant most hits never got their timestamp out at all.
        float offset = std::clamp(std::get<FloatValue>(stackLatencyDelayMs).value, 0.f,
                                  static_cast<float>(maxLatencyMs));
        bool usedOverride = false;
        if (pendingReportedOffset >= 0.f) {
            offset = pendingReportedOffset;
            usedOverride = true;
            // Consume it immediately. CubeCraft sends these ~60x/second, so a
            // time-windowed override got stamped onto dozens of consecutive echoes and
            // pinned the server's ping estimate at one ghost's age -- every later hit
            // at a different age was then validated against the wrong instant.
            pendingReportedOffset = -1.f;
        }
        if (offset > 0.f) {
            auto base = reinterpret_cast<uintptr_t>(packet);
            auto* timestamp = reinterpret_cast<uint64_t*>(base + 0x30);
            uint64_t offsetUs = static_cast<uint64_t>(offset * 1000.0f);
            bool applied = *timestamp > offsetUs;
            if (applied) *timestamp -= offsetUs;
            // Only log overrides. The slider path fires ~60x/second on CubeCraft and
            // would bury everything else in the trace.
            if (usedOverride) {
                BT_LOG("[BT] nsl echo OVERRIDE offset={:.0f} raw={} applied={}", offset, *timestamp,
                       applied ? 1 : 0);
            }
        }
    }

    uint64_t target = 0;
    if (!isAttackPacket(packet, target)) return;

    auto now = std::chrono::steady_clock::now();

    if (swallowRID != 0 && target == swallowRID && now < swallowUntil) {
        BT_LOG("[BT] send SWALLOW rid={} (already queued, cancelling duplicate)", target);
        ev.setCancelled(true);
        return;
    }

    if (pendingRID == 0 || target != pendingRID) return;
    if (now - pendingAt > std::chrono::milliseconds(150)) {
        BT_LOG("[BT] send STALE rid={}: pending older than 150ms, letting through", target);
        pendingRID = 0;
        return;
    }

    // This is the "hit the live model" path, so capture the ghost record for that
    // target now. Prefer the age of the ghost actually under the crosshair: with
    // several ghosts on screen the full-window one is usually not the one aimed at,
    // and building the attack from a different moment than the box you clicked is
    // exactly why those hits did nothing.
    float useAge = aimedGhostAge >= 0.f ? aimedGhostAge : ghostAgeMsFor(pendingRID);
    bool ghostFresh = false;
    Sample ghostBlend {};
    const Sample* ghostSample = nullptr;
    if (!std::get<BoolValue>(onlyLastRecord) && interpolateSample(pendingRID, useAge, ghostBlend)) {
        ghostSample = &ghostBlend;
    } else {
        ghostSample = findSample(pendingRID, useAge, ghostFresh);
    }
    if (!ghostSample) {
        BT_LOG("[BT] send NOGHOST rid={} age={:.0f}ms: no record, letting attack through", target, useAge);
        pendingRID = 0;
        return;
    }
    // Airborne records are drawn but not attackable, so let the vanilla attack through
    // untouched rather than redirecting it at a record we consider invalid.
    if (std::get<BoolValue>(invalidateAirborne) && ghostSample->airborne) {
        BT_LOG("[BT] send AIRBORNE rid={} age={:.0f}ms: record invalidated, letting attack through", target, useAge);
        pendingRID = 0;
        return;
    }

    // Report a timestamp consistent with that ghost's age, so the server rewinds to
    // the moment we aimed at rather than the one the slider happens to hold.
    pendingReportedOffset = adjustedOffsetFor(useAge);
    reportedOffsetUntil = now + std::chrono::milliseconds(reportedOffsetHoldMs);

    // Aim point on the ghost: re-cast the crosshair so the reported spot is the half
    // of the box being looked at. Falling back to the centre made repeated hits on
    // one half all claim an identical point.
    Vec3 hitPoint { ghostSample->pos.x, (ghostSample->box.lower.y + ghostSample->box.higher.y) * 0.5f,
                    ghostSample->pos.z };
    Vec3 eye {};
    Vec3 dir {};
    if (cameraRay(eye, dir)) {
        if (auto d = ghostSample->box.intersectsRay(eye, dir, legitReach, 0.f)) {
            hitPoint = pushInsideBox(Vec3 { eye.x + dir.x * *d, eye.y + dir.y * *d, eye.z + dir.z * *d },
                                     ghostSample->box);
        }
    }

    attackQueue.push_back({ pendingRID, pendingAt, ghostSample->pos, ghostSample->box, hitPoint,
                            pendingReportedOffset });
    while (attackQueue.size() > 16) attackQueue.pop_front();

    swallowRID = pendingRID;
    swallowUntil = now + std::chrono::milliseconds(150);
    pendingRID = 0;
    ev.setCancelled(true);

}

void Backtrack::sendLatencyProbe(float offsetMs) {
    if (offsetMs <= 0.f) return;
    if (!Signatures::MinecraftPackets_createPacket.result) return;

    auto ci = SDK::ClientInstance::get();
    if (!ci) return;
    auto* lp = ci->getLocalPlayer();
    if (!lp || !lp->packetSender) return;

    // Build our own NetworkStackLatency instead of waiting for the game to echo one.
    // The client only replies when the server asks it to, so relying on that meant our
    // modified timestamp was never actually transmitted -- the log showed zero outgoing
    // echoes across dozens of hits.
    auto pkt = SDK::MinecraftPackets::createPacket(SDK::PacketID::NETWORK_STACK_LATENCY);
    if (!pkt) return;

    auto base = reinterpret_cast<uintptr_t>(pkt.get());
    // +0x30 is "Creation Time" in microseconds (confirmed: the serializer at
    // 0x14288B970 labels a1+6 with that string). Backdating it by the offset makes the
    // server measure a correspondingly larger round trip.
    uint64_t nowUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
    uint64_t offsetUs = static_cast<uint64_t>(offsetMs * 1000.f);
    *reinterpret_cast<uint64_t*>(base + 0x30) = (nowUs > offsetUs) ? (nowUs - offsetUs) : 0;
    // +0x38 asks the peer to send it back. Without it this is a one-way report.
    *reinterpret_cast<uint8_t*>(base + 0x38) = 1;

    probeInFlight = true;  // let our own send hook pass this through untouched
    lp->packetSender->sendToServer(pkt.get());
    probeInFlight = false;

    BT_LOG("[BT] probe SENT offset={:.0f} ts={}", offsetMs, *reinterpret_cast<uint64_t*>(base + 0x30));
}

bool Backtrack::getGhostBox(uint64_t runtimeID, AABB& out) {
    if (!isEnabled()) return false;

    float age = ghostAgeMsFor(runtimeID);
    bool fresh = false;
    Sample blended {};
    const Sample* sample = nullptr;
    // Same order the renderer and the target picker use, so an external caller aims at
    // exactly the box that is drawn and hittable.
    if (!std::get<BoolValue>(onlyLastRecord) && interpolateSample(runtimeID, age, blended)) {
        sample = &blended;
    } else {
        sample = findSample(runtimeID, age, fresh);
    }
    if (!sample) return false;
    // Airborne records stay drawn but are not offered to anything that attacks, so
    // Aimbot and Triggerbot skip them without the box disappearing.
    if (std::get<BoolValue>(invalidateAirborne) && sample->airborne) return false;

    out = sample->box;
    return true;
}

void Backtrack::onReceivePacket(Event&) {
}

bool Backtrack::isAttackPacket(SDK::Packet* packet, uint64_t& outTarget) {
    auto id = packet->getID();
    auto base = reinterpret_cast<uintptr_t>(packet);

    if (id == SDK::PacketID::INTERACT) {
        if (*reinterpret_cast<uint8_t*>(base + 0x30) != 2) return false;
        outTarget = *reinterpret_cast<uint64_t*>(base + 0x38);
        return true;
    }

    if (id == SDK::PacketID::PLAYER_AUTH_INPUT) {
        auto txn = *reinterpret_cast<uintptr_t*>(base + 0xB0);
        if (!txn) return false;
        if (*reinterpret_cast<uint32_t*>(txn + 0x70) != 1) return false;
        outTarget = *reinterpret_cast<uint64_t*>(txn + 0x68);
        return true;
    }

    return false;
}

SDK::Actor* Backtrack::resolveActor(uint64_t runtimeID) {
    auto* actor = EntityCache::get().findByRuntimeID(runtimeID);
    if (!actor || !actor->isPlayer()) return nullptr;
    auto hp = actor->getHealth();
    if (!hp || *hp <= 0.f) return nullptr;
    return actor;
}

bool Backtrack::interpolateSample(uint64_t runtimeID, float ageMs, Sample& out) {
    auto it = buffers.find(runtimeID);
    if (it == buffers.end() || it->second.size() < 2) return false;

    auto& buf = it->second;
    // While frozen, ages are measured from the instant the freeze began, so every
    // ghost stays pinned where it was instead of sliding along with the enemy.
    auto ref = freezeActive ? freezeAt : std::chrono::steady_clock::now();
    auto target = ref - std::chrono::milliseconds(static_cast<int>(ageMs));

    // Walk newest to oldest for the first pair that brackets the requested instant.
    for (size_t i = buf.size(); i-- > 1;) {
        const Sample& newer = buf[i];
        const Sample& older = buf[i - 1];
        if (older.at > target || newer.at < target) continue;

        float span = std::chrono::duration<float, std::milli>(newer.at - older.at).count();
        if (span <= 0.f) {
            out = older;
            return true;
        }
        float t = std::clamp(std::chrono::duration<float, std::milli>(target - older.at).count() / span, 0.f, 1.f);

        out.at = target;
        out.pos = older.pos + (newer.pos - older.pos) * t;
        // Interpolate the box corners rather than rebuilding from the position, so
        // a hitbox that changed size (sneaking, mounting) stays correct mid-blend.
        out.box.lower = older.box.lower + (newer.box.lower - older.box.lower) * t;
        out.box.higher = older.box.higher + (newer.box.higher - older.box.higher) * t;
        // Take the state of whichever sample the blend actually sits closer to. OR-ing
        // the pair was wrong: isOnGround() drops for a tick on ordinary movement -- a
        // slope, a block edge -- and one such sample would then invalidate every blend
        // touching it, which is a 40ms window either side of a player who never left
        // the ground.
        out.airborne = (t < 0.5f) ? older.airborne : newer.airborne;
        out.rawAirborne = (t < 0.5f) ? older.rawAirborne : newer.rawAirborne;
        return true;
    }
    return false;
}

void Backtrack::buildGhostAges(std::vector<float>& out, uint64_t runtimeID) {
    out.clear();
    // Per-target, because New Record Per Hit shifts each player's window independently.
    float maxAge = ghostAgeMsFor(runtimeID);

    if (std::get<BoolValue>(onlyLastRecord)) {
        out.push_back(maxAge);
        return;
    }

    int count = static_cast<int>(std::clamp(std::get<FloatValue>(ghostCount).value, 1.f, 12.f));
    if (count <= 1 || maxAge <= 0.f) {
        out.push_back(maxAge);
        return;
    }

    // Spread the ghosts evenly from the live model back to the full window. i starts
    // at 1 so we never emit age 0, which would sit on top of the real model.
    for (int i = 1; i <= count; ++i) {
        out.push_back(maxAge * (static_cast<float>(i) / static_cast<float>(count)));
    }
}

float Backtrack::adjustedOffsetFor(float ghostAge) {
    if (ghostAge < 0.f) return -1.f;
    // Fake Latency is a real network delay; it cannot be retimed per hit, so when it
    // is doing the work we leave everything alone rather than desyncing ourselves.
    if (std::get<FloatValue>(fakeLatencyMs).value > 0.f) return -1.f;

    // Report the ghost's full age. Backtrack Time must NOT be subtracted: it is a
    // local choice about which stored position we draw, and the server has no idea
    // it exists. The server rewinds purely by the latency our echo implies, so that
    // number has to be the whole age. Subtracting it made every near ghost resolve
    // to a smaller offset -- often clamped to 0 -- so the server rewound to the
    // wrong instant and only the ghost matching the raw slider ever connected.
    return std::clamp(ghostAge, 0.f, static_cast<float>(maxLatencyMs));
}

const Backtrack::Sample* Backtrack::findSample(uint64_t runtimeID, float ageMs, bool& fresh) {
    fresh = false;
    auto it = buffers.find(runtimeID);
    if (it == buffers.end() || it->second.empty()) return nullptr;

    auto& buf = it->second;
    auto now = freezeActive ? freezeAt : std::chrono::steady_clock::now();
    auto target = now - std::chrono::milliseconds(static_cast<int>(ageMs));

    const Sample* best = nullptr;
    for (auto rit = buf.rbegin(); rit != buf.rend(); ++rit) {
        if (rit->at <= target) {
            best = &*rit;
            break;
        }
    }
    // No sample is actually as old as the requested window. We used to fall back
    // to the oldest sample we had, which reported (say) a 200ms position as if it
    // were the 500ms one the slider asked for -- the box you aimed at did not
    // match the position the attack was built from, so the hit never landed.
    if (!best) return nullptr;

    float bestAge = std::chrono::duration<float, std::milli>(now - best->at).count();
    fresh = std::abs(bestAge - ageMs) <= sampleToleranceMs;

    // Only Last Record: use strictly the record at the configured age. Without
    // this the nearest older sample is accepted, which on a laggy stream can be
    // well past the window and puts the box somewhere the slider never asked for.
    if (std::get<BoolValue>(onlyLastRecord) && !fresh) return nullptr;

    return best;
}

void Backtrack::onRenderLevel(RenderLevelEvent&) {
    if (AntiObs::isActive()) return;
    if (!std::get<BoolValue>(hitbox)) return;

    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->levelRenderer || !SDK::ScreenContext::instance3d) return;
    auto lp = ci->getLocalPlayer();
    if (!lp) return;

    auto material = std::get<BoolValue>(throughWalls) ? SDK::MaterialPtr::getSelectionOverlayMaterial()
                                                      : SDK::MaterialPtr::getSelectionBoxMaterial();
    MCDrawUtil3D dc(ci->levelRenderer, SDK::ScreenContext::instance3d, material);

    bool onlyLast = std::get<BoolValue>(onlyLastRecord);
    auto baseCol = std::get<ColorValue>(hitboxColor).getMainColor();
    int style = hitboxStyle.getSelectedKey();

    // A box per ghost age per enemy -- no aiming required. Ages come from the same
    // helper the target picker uses, so everything drawn here is hittable.
    auto snap = EntityCache::get().snapshot();
    for (auto* actor : snap->actors) {
        if (!actor || actor == lp || !actor->isPlayer()) continue;
        if (actor->isInvisible()) continue;

        uint64_t rid = actor->getRuntimeID();
        // Per target, matching the picker: each player's window shifts independently.
        buildGhostAges(ghostAgeScratch, rid);
        for (float age : ghostAgeScratch) {
            bool fresh = false;
            Sample blended {};
            const Sample* sample = nullptr;
            if (!onlyLast && interpolateSample(rid, age, blended)) {
                sample = &blended;
            } else {
                sample = findSample(rid, age, fresh);
            }
            if (!sample) continue;

            AABB shifted = sample->box;
            d2d::Color col(baseCol);

            if (style != style_outline) {
                Vec3 lo = shifted.lower;
                Vec3 hi = shifted.higher;
                dc.fillQuad({ lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { hi.x, lo.y, hi.z }, { lo.x, lo.y, hi.z }, col);
                dc.fillQuad({ lo.x, hi.y, lo.z }, { hi.x, hi.y, lo.z }, { hi.x, hi.y, hi.z }, { lo.x, hi.y, hi.z }, col);
                dc.fillQuad({ lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { hi.x, hi.y, lo.z }, { lo.x, hi.y, lo.z }, col);
                dc.fillQuad({ lo.x, lo.y, hi.z }, { hi.x, lo.y, hi.z }, { hi.x, hi.y, hi.z }, { lo.x, hi.y, hi.z }, col);
                dc.fillQuad({ lo.x, lo.y, lo.z }, { lo.x, lo.y, hi.z }, { lo.x, hi.y, hi.z }, { lo.x, hi.y, lo.z }, col);
                dc.fillQuad({ hi.x, lo.y, lo.z }, { hi.x, lo.y, hi.z }, { hi.x, hi.y, hi.z }, { hi.x, hi.y, lo.z }, col);
            }
            if (style != style_filled) {
                d2d::Color lineCol = col;
                lineCol.a = std::max(lineCol.a, 0.9f);
                dc.drawBox(shifted, lineCol);
            }
            dc.flush();
        }
    }
}
