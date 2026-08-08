#include "pch.h"
#include "AntiAFK.h"
#include "client/event/events/AfterMoveEvent.h"
#include "client/event/events/AttackEvent.h"
#include "client/event/events/ClickEvent.h"
#include "client/event/events/KeyUpdateEvent.h"
#include "client/event/events/MouseWheelEvent.h"
#include "client/event/events/TickEvent.h"
#include "client/event/events/TurnDeltaEvent.h"
#include "client/screen/ScreenManager.h"
#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/player/LocalPlayer.h"
#include "mc/common/world/level/BlockSource.h"

namespace {
    float wrapAngle(float angle) {
        while (angle > 180.f) angle -= 360.f;
        while (angle < -180.f) angle += 360.f;
        return angle;
    }

    int floori(float v) {
        return static_cast<int>(std::floor(v));
    }

    Vec3 feetPos(SDK::LocalPlayer* lp) {
        if (lp->aabbShape) {
            AABB& box = lp->aabbShape->boundingBox;
            return { (box.lower.x + box.higher.x) * 0.5f, box.lower.y, (box.lower.z + box.higher.z) * 0.5f };
        }
        Vec3 pos = lp->getPos();
        return { pos.x, pos.y - 1.62f, pos.z };
    }

    BlockPos blockAt(Vec3 const& pos) {
        return { floori(pos.x), floori(pos.y + 0.05f), floori(pos.z) };
    }
}

AntiAFK::AntiAFK()
    : Module("AntiAFK", LocalizeString::get("client.module.antiafk.name"),
             LocalizeString::get("client.module.antiafk.desc"), GAME, nokeybind) {
    addSliderSetting("interval", LocalizeString::get("client.module.antiafk.interval.name"),
                     LocalizeString::get("client.module.antiafk.interval.desc"), interval, FloatValue(5.f),
                     FloatValue(120.f), FloatValue(5.f));
    addSetting("jitter", LocalizeString::get("client.module.antiafk.jitter.name"),
               LocalizeString::get("client.module.antiafk.jitter.desc"), jitter);
    addSetting("move", LocalizeString::get("client.module.antiafk.move.name"),
               LocalizeString::get("client.module.antiafk.move.desc"), move);
    addSetting("spin", LocalizeString::get("client.module.antiafk.spin.name"),
               LocalizeString::get("client.module.antiafk.spin.desc"), spin);
    addSetting("jump", LocalizeString::get("client.module.antiafk.jump.name"),
               LocalizeString::get("client.module.antiafk.jump.desc"), jump);

    listen<TickEvent>((EventListenerFunc)&AntiAFK::onTick);
    listen<AfterMoveEvent>((EventListenerFunc)&AntiAFK::onAfterMove);
    listen<KeyUpdateEvent>((EventListenerFunc)&AntiAFK::onUserInput);
    listen<ClickEvent>((EventListenerFunc)&AntiAFK::onUserInput);
    listen<MouseWheelEvent>((EventListenerFunc)&AntiAFK::onUserInput);
    listen<AttackEvent>((EventListenerFunc)&AntiAFK::onUserInput);
    listen<TurnDeltaEvent>((EventListenerFunc)&AntiAFK::onTurn);
}

void AntiAFK::onEnable() {
    phase = Phase::Waiting;
    driving = false;
    jumpTicks = 0;
    selfTurn = false;
    lastCycleEnd = Clock::now();
    markActivity();
    scheduleNext();
}

void AntiAFK::onDisable() {
    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    auto input = lp ? lp->getMoveInputComponent() : nullptr;
    if (input) releaseOwnInput(input);
    driving = false;
    jumpTicks = 0;
    phase = Phase::Waiting;
}

void AntiAFK::markActivity() {
    lastActivityNs.store(Clock::now().time_since_epoch().count(), std::memory_order_relaxed);
}

AntiAFK::Clock::time_point AntiAFK::lastActivity() const {
    return Clock::time_point(Clock::duration(lastActivityNs.load(std::memory_order_relaxed)));
}

void AntiAFK::onUserInput(Event&) {
    markActivity();
}

void AntiAFK::onTurn(Event& evG) {
    if (selfTurn) return;
    auto& ev = reinterpret_cast<TurnDeltaEvent&>(evG);
    Vec2 const& d = ev.getDelta();
    if (std::abs(d.x) < 0.05f && std::abs(d.y) < 0.05f) return;
    markActivity();
}

bool AntiAFK::userIsMoving(SDK::MoveInputComponent* input) const {
    auto const& raw = input->rawInputState;
    if (raw.up || raw.down || raw.left || raw.right) return true;
    if (raw.jumpDown || raw.sneakDown) return true;
    if (std::abs(raw.analogMoveVector.x) > 0.01f || std::abs(raw.analogMoveVector.y) > 0.01f) return true;
    return false;
}

void AntiAFK::onTick(Event&) {
    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (!lp) return;

    auto input = lp->getMoveInputComponent();
    if (!input) return;

    auto now = Clock::now();

    if (userIsMoving(input)) markActivity();

    if (phase != Phase::Waiting && lastActivity() > cycleStart) {
        abortCycle(input);
        return;
    }

    if (Necromancer::get().getScreenManager().getActiveScreen()) {
        if (phase != Phase::Waiting) abortCycle(input);
        return;
    }

    switch (phase) {
    case Phase::Waiting: {
        auto readyAt = std::max(lastActivity(), lastCycleEnd) + currentWaitDelay();
        if (now < readyAt) return;

        if (std::get<BoolValue>(spin)) {
            std::uniform_real_distribution<float> spinDist(15.f, 45.f);
            std::uniform_int_distribution<int> signDist(0, 1);
            float amt = spinDist(rng) * (signDist(rng) ? 1.f : -1.f);
            turnBy(lp, amt);
        }

        if (std::get<BoolValue>(move)) {
            if (auto dest = pickDestination(lp)) {
                walkTarget = *dest;
                BlockPos homeBlock = blockAt(feetPos(lp));
                homePos = { homeBlock.x + 0.5f, static_cast<float>(homeBlock.y), homeBlock.z + 0.5f };
                cycleStart = now;
                phaseStart = now;
                lastProgressAt = now;
                lastDist = std::numeric_limits<float>::max();
                driving = true;
                phase = Phase::WalkingOut;
                return;
            }
        }

        if (std::get<BoolValue>(jump)) {
            jumpTicks = 3;
        }

        lastCycleEnd = now;
        scheduleNext();
        break;
    }
    case Phase::WalkingOut: {
        bool timedOut = std::chrono::duration<float>(now - phaseStart).count() > 4.f;
        if (timedOut || driveToward(lp, walkTarget)) {
            driving = false;
            std::uniform_real_distribution<float> dwellDist(0.3f, 0.8f);
            dwellUntil = now + std::chrono::milliseconds(static_cast<int>(dwellDist(rng) * 1000.f));
            phase = Phase::Dwell;
        }
        break;
    }
    case Phase::Dwell: {
        if (now < dwellUntil) return;
        phaseStart = now;
        lastProgressAt = now;
        lastDist = std::numeric_limits<float>::max();
        driving = true;
        phase = Phase::Returning;
        break;
    }
    case Phase::Returning: {
        bool timedOut = std::chrono::duration<float>(now - phaseStart).count() > 4.f;
        if (timedOut || driveToward(lp, homePos)) {
            driving = false;
            releaseOwnInput(input);
            phase = Phase::Waiting;
            lastCycleEnd = now;
            scheduleNext();
        }
        break;
    }
    }
}

void AntiAFK::onAfterMove(Event& evG) {
    auto& ev = reinterpret_cast<AfterMoveEvent&>(evG);
    auto input = ev.getMoveInputHandler();
    if (!input) return;

    if (driving) {
        input->inputState.up = true;
        input->inputState.analogMoveVector = { 0.f, 1.f };
    }
    if (jumpTicks > 0) {
        --jumpTicks;
        input->inputState.jumpDown = true;
        input->jumping = true;
    }
}

void AntiAFK::abortCycle(SDK::MoveInputComponent* input) {
    driving = false;
    jumpTicks = 0;
    releaseOwnInput(input);
    phase = Phase::Waiting;
    lastCycleEnd = Clock::now();
    scheduleNext();
}

void AntiAFK::releaseOwnInput(SDK::MoveInputComponent* input) {
    input->inputState.up = false;
    input->inputState.jumpDown = false;
    input->inputState.analogMoveVector = { 0.f, 0.f };
    input->jumping = false;
}

void AntiAFK::turnBy(SDK::LocalPlayer* lp, float yawDelta) {
    selfTurn = true;
    lp->applyTurnDelta(Vec2 { 0.f, yawDelta });
    selfTurn = false;
}

bool AntiAFK::rayHitsSolid(SDK::BlockSource* region, Vec3 const& start, Vec3 const& end) {
    int x = floori(start.x);
    int y = floori(start.y);
    int z = floori(start.z);
    if (region->isSolidBlockingBlockAt({ x, y, z })) return true;

    Vec3 delta = end - start;
    float len = delta.magnitude();
    if (len < 1e-4f) return false;
    Vec3 dir = delta * (1.f / len);

    constexpr float inf = std::numeric_limits<float>::infinity();
    auto tInit = [](float s, float d) {
        if (d > 0.f) return (std::floor(s) + 1.f - s) / d;
        if (d < 0.f) return (s - std::floor(s)) / -d;
        return inf;
    };

    int stepX = dir.x > 0.f ? 1 : -1;
    int stepY = dir.y > 0.f ? 1 : -1;
    int stepZ = dir.z > 0.f ? 1 : -1;
    float tMaxX = tInit(start.x, dir.x);
    float tMaxY = tInit(start.y, dir.y);
    float tMaxZ = tInit(start.z, dir.z);
    float tDeltaX = dir.x != 0.f ? std::abs(1.f / dir.x) : inf;
    float tDeltaY = dir.y != 0.f ? std::abs(1.f / dir.y) : inf;
    float tDeltaZ = dir.z != 0.f ? std::abs(1.f / dir.z) : inf;

    for (int i = 0; i < 128; ++i) {
        float t;
        if (tMaxX <= tMaxY && tMaxX <= tMaxZ) {
            t = tMaxX;
            tMaxX += tDeltaX;
            x += stepX;
        } else if (tMaxY <= tMaxZ) {
            t = tMaxY;
            tMaxY += tDeltaY;
            y += stepY;
        } else {
            t = tMaxZ;
            tMaxZ += tDeltaZ;
            z += stepZ;
        }
        if (t > len) break;
        if (region->isSolidBlockingBlockAt({ x, y, z })) return true;
    }
    return false;
}

bool AntiAFK::groundBelow(SDK::BlockSource* region, Vec3 const& feetCenter) {
    Vec3 top { feetCenter.x, feetCenter.y + 0.05f, feetCenter.z };
    Vec3 bottom { feetCenter.x, feetCenter.y - 1.45f, feetCenter.z };
    return rayHitsSolid(region, top, bottom);
}

bool AntiAFK::bodyClear(SDK::BlockSource* region, BlockPos const& bp) {
    if (region->isSolidBlockingBlockAt(bp)) return false;
    if (region->isSolidBlockingBlockAt({ bp.x, bp.y + 1, bp.z })) return false;
    return true;
}

bool AntiAFK::pathClear(SDK::BlockSource* region, Vec3 const& fromFeet, Vec3 const& toFeet) {
    Vec3 fromLow { fromFeet.x, fromFeet.y + 0.6f, fromFeet.z };
    Vec3 toLow { toFeet.x, toFeet.y + 0.6f, toFeet.z };
    if (rayHitsSolid(region, fromLow, toLow)) return false;

    Vec3 fromHigh { fromFeet.x, fromFeet.y + 1.4f, fromFeet.z };
    Vec3 toHigh { toFeet.x, toFeet.y + 1.4f, toFeet.z };
    if (rayHitsSolid(region, fromHigh, toHigh)) return false;
    return true;
}

bool AntiAFK::isWalkable(SDK::BlockSource* region, BlockPos const& bp) {
    if (!bodyClear(region, bp)) return false;
    Vec3 center { bp.x + 0.5f, static_cast<float>(bp.y), bp.z + 0.5f };
    return groundBelow(region, center);
}

std::optional<Vec3> AntiAFK::pickDestination(SDK::LocalPlayer* lp) {
    auto ci = SDK::ClientInstance::get();
    auto region = ci ? ci->getRegion() : nullptr;
    if (!region) return std::nullopt;

    Vec3 feet = feetPos(lp);
    BlockPos feetBp = blockAt(feet);
    Vec3 fromFeet { feetBp.x + 0.5f, static_cast<float>(feetBp.y), feetBp.z + 0.5f };

    if (!groundBelow(region, fromFeet)) return std::nullopt;

    static constexpr BlockPos dirs[4] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

    std::vector<Vec3> candidates;
    for (int dy : { 0, 1, -1 }) {
        for (auto const& d : dirs) {
            BlockPos bp { feetBp.x + d.x, feetBp.y + dy, feetBp.z + d.z };
            if (!isWalkable(region, bp)) continue;
            Vec3 destFeet { bp.x + 0.5f, static_cast<float>(bp.y), bp.z + 0.5f };
            if (!pathClear(region, fromFeet, destFeet)) continue;
            candidates.push_back(destFeet);
        }
        if (!candidates.empty()) break;
    }
    if (candidates.empty()) return std::nullopt;

    std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
    return candidates[pick(rng)];
}

bool AntiAFK::driveToward(SDK::LocalPlayer* lp, Vec3 const& target) {
    Vec3 feet = feetPos(lp);
    float dx = target.x - feet.x;
    float dz = target.z - feet.z;
    float dist = std::hypot(dx, dz);

    if (dist < 0.3f && std::abs(target.y - feet.y) < 1.2f) return true;

    auto now = Clock::now();

    float desiredYaw = std::atan2(dz, dx) * (180.f / pi_f) - 90.f;
    float yawErr = wrapAngle(desiredYaw - lp->getRot().y);
    turnBy(lp, std::clamp(yawErr, -20.f, 20.f));

    if (lastDist - dist > 0.02f) {
        lastDist = dist;
        lastProgressAt = now;
    } else if (std::get<BoolValue>(jump) && std::chrono::duration<float>(now - lastProgressAt).count() > 0.6f) {
        jumpTicks = 2;
        lastProgressAt = now;
    }

    return false;
}

void AntiAFK::scheduleNext() {
    jitterMult = 1.f;
    if (std::get<BoolValue>(jitter)) {
        std::uniform_real_distribution<float> jd(0.5f, 1.5f);
        jitterMult = jd(rng);
    }
    currentDelay = currentWaitDelay();
}

std::chrono::nanoseconds AntiAFK::currentWaitDelay() const {
    float base = std::get<FloatValue>(interval).value;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<float>(base * jitterMult));
}
