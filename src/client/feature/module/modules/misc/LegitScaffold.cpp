#include "pch.h"
#include "LegitScaffold.h"

#include "client/event/events/BuildBlockEvent.h"
#include "client/event/events/TickEvent.h"
#include "client/Necromancer.h"
#include "client/screen/ScreenManager.h"
#include "client/misc/BlockSolid.h"
#include "client/misc/MovementSim.h"
#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/player/LocalPlayer.h"
#include "mc/common/world/actor/player/PlayerInventory.h"
#include "mc/common/world/actor/player/Inventory.h"
#include "mc/common/world/actor/player/GameMode.h"
#include "mc/common/world/level/BlockSource.h"
#include "mc/common/world/ItemStack.h"

namespace {
    constexpr float pi_f = 3.14159265358979323846f;

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

    Vec3 eyePos(SDK::LocalPlayer* lp) {
        Vec3 feet = feetPos(lp);
        float height = 1.62f;
        if (lp->aabbShape) {
            AABB& box = lp->aabbShape->boundingBox;
            float full = box.higher.y - box.lower.y;
            if (full > 0.1f) height = full - 0.18f;
        }
        return { feet.x, feet.y + height, feet.z };
    }

    Vec3 flatLookDir(Vec2 const& rot) {
        float yaw = (rot.y + 90.f) * (pi_f / 180.f);
        Vec3 dir { cosf(yaw), 0.f, sinf(yaw) };
        float len = sqrtf(dir.x * dir.x + dir.z * dir.z);
        if (len < 0.0001f) return { 0.f, 0.f, 0.f };
        return { dir.x / len, 0.f, dir.z / len };
    }

    bool sameBlock(BlockPos const& a, BlockPos const& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    bool isReplaceable(SDK::BlockSource* region, BlockPos const& pos) {
        return !BlockSolid::isCollidable(region, pos.x, pos.y, pos.z);
    }

    bool isSupport(SDK::BlockSource* region, BlockPos const& pos) {
        return BlockSolid::isCollidable(region, pos.x, pos.y, pos.z);
    }
}

LegitScaffold::LegitScaffold()
    : Module("LegitScaffold", LocalizeString::get("client.module.legitScaffold.name"),
             LocalizeString::get("client.module.legitScaffold.desc"), GAME, nokeybind) {
    listen<BuildBlockEvent>(static_cast<EventListenerFunc>(&LegitScaffold::onBuildBlock));
    listen<TickEvent>(static_cast<EventListenerFunc>(&LegitScaffold::onTick));

    addSetting("allowUp", LocalizeString::get("client.module.legitScaffold.allowUp.name"),
               LocalizeString::get("client.module.legitScaffold.allowUp.desc"), allowUp);
    addSetting("speedBridge", LocalizeString::get("client.module.legitScaffold.speedBridge.name"),
               LocalizeString::get("client.module.legitScaffold.speedBridge.desc"), speedBridge);
    addSliderSetting("bridgeReach", LocalizeString::get("client.module.legitScaffold.bridgeReach.name"),
                     LocalizeString::get("client.module.legitScaffold.bridgeReach.desc"), bridgeReach, FloatValue(1.f),
                     FloatValue(5.f), FloatValue(0.5f), "speedBridge"_istrue);
    addSetting("straightOnly", LocalizeString::get("client.module.legitScaffold.straightOnly.name"),
               LocalizeString::get("client.module.legitScaffold.straightOnly.desc"), straightOnly,
               "speedBridge"_istrue);
    addSliderSetting("bridgeDrop", LocalizeString::get("client.module.legitScaffold.bridgeDrop.name"),
                     LocalizeString::get("client.module.legitScaffold.bridgeDrop.desc"), bridgeDrop, FloatValue(0.f),
                     FloatValue(5.f), FloatValue(1.f), "speedBridge"_istrue);
    addSliderSetting("bridgeDelay", LocalizeString::get("client.module.legitScaffold.bridgeDelay.name"),
                     LocalizeString::get("client.module.legitScaffold.bridgeDelay.desc"), bridgeDelay, FloatValue(0.f),
                     FloatValue(200.f), FloatValue(5.f), "speedBridge"_istrue);
    addSliderSetting("bridgeBlocksPerTick",
                     LocalizeString::get("client.module.legitScaffold.bridgeBlocksPerTick.name"),
                     LocalizeString::get("client.module.legitScaffold.bridgeBlocksPerTick.desc"), bridgeBlocksPerTick,
                     FloatValue(1.f), FloatValue(5.f), FloatValue(1.f), "speedBridge"_istrue);
    addSetting("requireHoldUse", LocalizeString::get("client.module.legitScaffold.requireHoldUse.name"),
               LocalizeString::get("client.module.legitScaffold.requireHoldUse.desc"), requireHoldUse,
               "speedBridge"_istrue);
    addSetting("requireMoving", LocalizeString::get("client.module.legitScaffold.requireMoving.name"),
               LocalizeString::get("client.module.legitScaffold.requireMoving.desc"), requireMoving,
               "speedBridge"_istrue);
    addSetting("requireLookDown", LocalizeString::get("client.module.legitScaffold.requireLookDown.name"),
               LocalizeString::get("client.module.legitScaffold.requireLookDown.desc"), requireLookDown,
               "speedBridge"_istrue);
    addSliderSetting("minPitch", LocalizeString::get("client.module.legitScaffold.minPitch.name"),
                     LocalizeString::get("client.module.legitScaffold.minPitch.desc"), minPitch, FloatValue(0.f),
                     FloatValue(89.f), FloatValue(1.f),
                     Setting::Condition(std::vector<Setting::SingleCond> {
                         { "speedBridge", { 1 }, false },
                         { "requireLookDown", { 1 }, false } }));

    Setting::Condition dirCond(std::vector<Setting::SingleCond> {
        { "speedBridge", { 1 }, false },
        { "directionBased", { 1 }, false },
    });

    addSetting("directionBased", LocalizeString::get("client.module.legitScaffold.directionBased.name"),
               LocalizeString::get("client.module.legitScaffold.directionBased.desc"), directionBased,
               "speedBridge"_istrue);
    addSetting("dirForward", LocalizeString::get("client.module.legitScaffold.dirForward.name"),
               LocalizeString::get("client.module.legitScaffold.dirForward.desc"), dirForward, dirCond);
    addSetting("dirBackward", LocalizeString::get("client.module.legitScaffold.dirBackward.name"),
               LocalizeString::get("client.module.legitScaffold.dirBackward.desc"), dirBackward, dirCond);
    addSetting("dirLeft", LocalizeString::get("client.module.legitScaffold.dirLeft.name"),
               LocalizeString::get("client.module.legitScaffold.dirLeft.desc"), dirLeft, dirCond);
    addSetting("dirRight", LocalizeString::get("client.module.legitScaffold.dirRight.name"),
               LocalizeString::get("client.module.legitScaffold.dirRight.desc"), dirRight, dirCond);
    addSetting("dirDiagonalFill", LocalizeString::get("client.module.legitScaffold.dirDiagonalFill.name"),
               LocalizeString::get("client.module.legitScaffold.dirDiagonalFill.desc"), dirDiagonalFill,
               Setting::Condition(std::vector<Setting::SingleCond> {
                   { "speedBridge", { 1 }, false },
                   { "directionBased", { 1 }, false },
                   { "straightOnly", { 0 }, false },
               }));
    addSliderSetting("dirSimTicks", LocalizeString::get("client.module.legitScaffold.dirSimTicks.name"),
                     LocalizeString::get("client.module.legitScaffold.dirSimTicks.desc"), dirSimTicks, FloatValue(1.f),
                     FloatValue(20.f), FloatValue(1.f), dirCond);
    addSliderSetting("dirMinSpeed", LocalizeString::get("client.module.legitScaffold.dirMinSpeed.name"),
                     LocalizeString::get("client.module.legitScaffold.dirMinSpeed.desc"), dirMinSpeed, FloatValue(0.f),
                     FloatValue(0.5f), FloatValue(0.01f), dirCond);
    addSliderSetting("dirBlendCamera", LocalizeString::get("client.module.legitScaffold.dirBlendCamera.name"),
                     LocalizeString::get("client.module.legitScaffold.dirBlendCamera.desc"), dirBlendCamera,
                     FloatValue(0.f), FloatValue(1.f), FloatValue(0.05f), dirCond);
}

int LegitScaffold::blocksPerPlacement() const {
    if (!std::get<BoolValue>(speedBridge).value) return 0;
    return std::max(1, static_cast<int>(std::get<FloatValue>(bridgeBlocksPerTick).value));
}

void LegitScaffold::onDisable() {
    bridging = false;
    hasBridgeY = false;
    bridgeY = 0;
    nextPlace = {};
}

void LegitScaffold::onBuildBlock(Event& evG) {
    auto& ev = reinterpret_cast<BuildBlockEvent&>(evG);

    if (bridging) return;

    auto ci = SDK::ClientInstance::get();
    auto plr = ci ? ci->getLocalPlayer() : nullptr;
    if (!plr || !plr->supplies || !plr->supplies->inventory) return;

    int sel = plr->supplies->selectedSlot;
    if (sel < 0 || sel >= 9) return;

    auto held = plr->supplies->inventory->getItem(sel);
    if (!held || held->itemCount <= 0 || !held->block) return;

    if (!shouldAllow(ev.getBlockPos(), ev.getFace())) {
        ev.setCancelled();
    }
}

bool LegitScaffold::findSupport(SDK::BlockSource* region, BlockPos const& target, BlockPos& outSupport,
                               uint8_t& outFace) const {
    static constexpr int offX[6] = { 0, 0, 0, 0, -1, 1 };
    static constexpr int offY[6] = { -1, 1, 0, 0, 0, 0 };
    static constexpr int offZ[6] = { 0, 0, -1, 1, 0, 0 };
    static constexpr uint8_t opposite[6] = { FACE_UP,   FACE_DOWN, FACE_SOUTH,
                                             FACE_NORTH, FACE_EAST, FACE_WEST };
    static constexpr int order[6] = { FACE_DOWN, FACE_NORTH, FACE_SOUTH, FACE_WEST, FACE_EAST, FACE_UP };

    for (int i = 0; i < 6; i++) {
        int f = order[i];
        if (opposite[f] == FACE_DOWN) continue;
        if (opposite[f] == FACE_UP && !std::get<BoolValue>(allowUp).value) continue;

        BlockPos neighbour { target.x + offX[f], target.y + offY[f], target.z + offZ[f] };
        if (!isSupport(region, neighbour)) continue;
        if (!isReplaceable(region, target)) continue;

        outSupport = neighbour;
        outFace = opposite[f];
        return true;
    }

    return false;
}

Vec3 LegitScaffold::resolveBridgeDir(SDK::Player* plr) const {
    auto* lp = static_cast<SDK::LocalPlayer*>(plr);
    Vec3 camera = flatLookDir(lp->getRot());

    auto snapAxis = [&](Vec3 dir) -> Vec3 {
        if (!std::get<BoolValue>(straightOnly).value) return dir;
        if (dir.x == 0.f && dir.z == 0.f) return dir;
        if (fabsf(dir.x) >= fabsf(dir.z)) return { dir.x > 0.f ? 1.f : -1.f, 0.f, 0.f };
        return { 0.f, 0.f, dir.z > 0.f ? 1.f : -1.f };
    };

    if (!std::get<BoolValue>(directionBased).value) return snapAxis(camera);

    auto* input = lp->getMoveInputComponent();
    if (!input) return snapAxis(camera);

    auto const& raw = input->rawInputState;
    bool wantFwd = raw.up || raw.upLeft || raw.upRight;
    bool wantBack = raw.down || raw.downLeft || raw.downRight;
    bool wantLeft = raw.left || raw.upLeft || raw.downLeft;
    bool wantRight = raw.right || raw.upRight || raw.downRight;

    if (!std::get<BoolValue>(dirForward).value) wantFwd = false;
    if (!std::get<BoolValue>(dirBackward).value) wantBack = false;
    if (!std::get<BoolValue>(dirLeft).value) wantLeft = false;
    if (!std::get<BoolValue>(dirRight).value) wantRight = false;

    if (wantFwd && wantBack) {
        wantFwd = false;
        wantBack = false;
    }
    if (wantLeft && wantRight) {
        wantLeft = false;
        wantRight = false;
    }

    if (!wantFwd && !wantBack && !wantLeft && !wantRight) return { 0.f, 0.f, 0.f };

    Vec3 right { -camera.z, 0.f, camera.x };
    Vec3 intent { 0.f, 0.f, 0.f };

    if (wantFwd) {
        intent.x += camera.x;
        intent.z += camera.z;
    }
    if (wantBack) {
        intent.x -= camera.x;
        intent.z -= camera.z;
    }
    if (wantRight) {
        intent.x += right.x;
        intent.z += right.z;
    }
    if (wantLeft) {
        intent.x -= right.x;
        intent.z -= right.z;
    }

    float intentLen = sqrtf(intent.x * intent.x + intent.z * intent.z);
    if (intentLen < 0.0001f) return { 0.f, 0.f, 0.f };
    intent.x /= intentLen;
    intent.z /= intentLen;

    int simTicks = std::clamp(static_cast<int>(std::get<FloatValue>(dirSimTicks).value), 1, 20);
    Vec3 velocity = lp->getVelocity();
    Vec3 seed { velocity.x, 0.f, velocity.z };

    float speed = sqrtf(seed.x * seed.x + seed.z * seed.z);
    float minSpeed = std::max(0.f, std::get<FloatValue>(dirMinSpeed).value);

    Vec3 simulated { 0.f, 0.f, 0.f };
    if (speed >= minSpeed) {
        auto forward = MovementSim::predictForward(lp, seed, simTicks, false);
        if (forward.valid) {
            Vec3 start = lp->getPos();
            simulated.x = forward.finalPos.x - start.x;
            simulated.z = forward.finalPos.z - start.z;

            float simLen = sqrtf(simulated.x * simulated.x + simulated.z * simulated.z);
            if (simLen >= 0.0001f) {
                simulated.x /= simLen;
                simulated.z /= simLen;
            } else {
                simulated = { 0.f, 0.f, 0.f };
            }
        }
    }

    Vec3 chosen = intent;
    if (simulated.x != 0.f || simulated.z != 0.f) {
        if (simulated.x * intent.x + simulated.z * intent.z > 0.f) {
            chosen = simulated;
        }
    }
    float blend = std::clamp(std::get<FloatValue>(dirBlendCamera).value, 0.f, 1.f);
    if (blend > 0.f) {
        chosen.x = chosen.x * (1.f - blend) + camera.x * blend;
        chosen.z = chosen.z * (1.f - blend) + camera.z * blend;
        float len = sqrtf(chosen.x * chosen.x + chosen.z * chosen.z);
        if (len < 0.0001f) return { 0.f, 0.f, 0.f };
        chosen.x /= len;
        chosen.z /= len;
    }

    return snapAxis(chosen);
}

void LegitScaffold::collectUnderCandidate(SDK::Player* plr, SDK::BlockSource* region, int placeY,
                                          std::vector<Candidate>& out) const {
    auto* lp = static_cast<SDK::LocalPlayer*>(plr);
    Vec3 feet = feetPos(lp);
    Vec3 eye = eyePos(lp);

    BlockPos candidate { floori(feet.x), placeY, floori(feet.z) };
    if (static_cast<float>(candidate.y) + 1.f > feet.y + 0.001f) return;
    if (!isReplaceable(region, candidate)) return;

    float cx = static_cast<float>(candidate.x) + 0.5f;
    float cy = static_cast<float>(candidate.y) + 0.5f;
    float cz = static_cast<float>(candidate.z) + 0.5f;
    float dx = cx - eye.x;
    float dy = cy - eye.y;
    float dz = cz - eye.z;
    if (sqrtf(dx * dx + dy * dy + dz * dz) > 5.5f) return;

    BlockPos support {};
    uint8_t face = FACE_NONE;
    if (!findSupport(region, candidate, support, face)) return;

    out.push_back(Candidate { support, face });
}

void LegitScaffold::collectCandidates(SDK::Player* plr, SDK::BlockSource* region, int placeY,
                                      std::vector<Candidate>& out) const {
    auto* lp = static_cast<SDK::LocalPlayer*>(plr);
    Vec3 feet = feetPos(lp);
    Vec3 forward = resolveBridgeDir(plr);
    if (forward.x == 0.f && forward.z == 0.f) forward = flatLookDir(lp->getRot());
    if (forward.x == 0.f && forward.z == 0.f) return;

    float reach = std::get<FloatValue>(bridgeReach).value;
    Vec3 eye = eyePos(lp);

    auto addCandidate = [&](BlockPos const& candidate) {
        if (!isReplaceable(region, candidate)) return;

        float cx = static_cast<float>(candidate.x) + 0.5f;
        float cy = static_cast<float>(candidate.y) + 0.5f;
        float cz = static_cast<float>(candidate.z) + 0.5f;
        float dx = cx - eye.x;
        float dy = cy - eye.y;
        float dz = cz - eye.z;
        if (sqrtf(dx * dx + dy * dy + dz * dz) > 5.5f) return;

        BlockPos support {};
        uint8_t face = FACE_NONE;
        if (!findSupport(region, candidate, support, face)) return;

        if (face != FACE_UP) {
            float sx = static_cast<float>(support.x) + 0.5f;
            float sz = static_cast<float>(support.z) + 0.5f;
            float toSupportX = sx - feet.x;
            float toSupportZ = sz - feet.z;
            float len = sqrtf(toSupportX * toSupportX + toSupportZ * toSupportZ);
            if (len > 1.5f) {
                if ((toSupportX / len) * forward.x + (toSupportZ / len) * forward.z < -0.35f) return;
            }
        }

        out.push_back(Candidate { support, face });
    };

    BlockPos under { floori(feet.x), placeY, floori(feet.z) };
    addCandidate(under);

    bool diagonalFill = std::get<BoolValue>(directionBased).value &&
                        std::get<BoolValue>(dirDiagonalFill).value &&
                        !std::get<BoolValue>(straightOnly).value;

    float step = 0.25f;
    BlockPos lastTried = under;

    for (float dist = step; dist <= reach + 0.0001f; dist += step) {
        BlockPos candidate { floori(feet.x + forward.x * dist), placeY, floori(feet.z + forward.z * dist) };
        if (sameBlock(candidate, lastTried)) continue;

        if (diagonalFill && candidate.x != lastTried.x && candidate.z != lastTried.z) {
            addCandidate(BlockPos { candidate.x, placeY, lastTried.z });
            addCandidate(BlockPos { lastTried.x, placeY, candidate.z });
        }

        lastTried = candidate;

        addCandidate(candidate);
    }
}

void LegitScaffold::onTick(Event&) {
    if (!std::get<BoolValue>(speedBridge)) {
        bridging = false;
        return;
    }

    auto ci = SDK::ClientInstance::get();
    auto plr = ci ? ci->getLocalPlayer() : nullptr;
    if (!plr || !plr->supplies || !plr->supplies->inventory || !plr->gameMode) return;
    if (Necromancer::get().getScreenManager().getActiveScreen().has_value()) return;
    if (!Signatures::GameMode_buildBlock.result) return;

    int sel = plr->supplies->selectedSlot;
    if (sel < 0 || sel >= 9) return;

    auto held = plr->supplies->inventory->getItem(sel);
    if (!held || held->itemCount <= 0 || !held->block) return;

    if (std::get<BoolValue>(requireHoldUse) && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0) return;

    auto* input = plr->getMoveInputComponent();
    if (std::get<BoolValue>(requireMoving)) {
        if (!input) return;
        auto const& raw = input->rawInputState;
        bool moving = raw.up || raw.down || raw.left || raw.right || raw.upLeft || raw.upRight || raw.downLeft ||
                      raw.downRight;
        if (!moving) return;
    }

    if (std::get<BoolValue>(requireLookDown)) {
        if (plr->getRot().x < std::get<FloatValue>(minPitch).value) return;
    }

    auto now = std::chrono::steady_clock::now();
    if (now < nextPlace) return;

    auto region = ci->getRegion();
    if (!region) return;

    Vec3 feet = feetPos(plr);
    int standY = floori(feet.y + 0.001f) - 1;
    bool onGround = plr->isOnGround();
    bool doAllowUp = std::get<BoolValue>(allowUp).value;
    Vec3 velocity = plr->getVelocity();

    if (onGround || !hasBridgeY) {
        bridgeY = standY;
        hasBridgeY = true;
    }

    int maxDrop = std::max(0, static_cast<int>(std::get<FloatValue>(bridgeDrop).value));
    if (bridgeY > standY) bridgeY = standY;
    if (standY - bridgeY > maxDrop) bridgeY = standY - maxDrop;

    int clearedY = floori(feet.y + 0.001f);
    float nextFeetY = feet.y + (velocity.y - 0.08f) * 0.98f;
    int predictedY = floori(std::min(feet.y, nextFeetY) + 0.001f);
    int towerY = std::max(clearedY, predictedY) - 1;

    std::vector<Candidate> candidates;
    bool towerUp = doAllowUp && !onGround && towerY > bridgeY;
    if (towerUp) {
        collectUnderCandidate(plr, region, towerY, candidates);
        if (!candidates.empty()) bridgeY = towerY;
    }
    if (candidates.empty()) collectCandidates(plr, region, bridgeY, candidates);
    if (candidates.empty()) return;

    bridging = true;
    int placedCount = 0;
    int maxPerTick = std::max(1, static_cast<int>(std::get<FloatValue>(bridgeBlocksPerTick).value));

    for (int pass = 0; pass < maxPerTick; pass++) {
        if (pass > 0) {
            int liveSlot = plr->supplies->selectedSlot;
            if (liveSlot < 0 || liveSlot >= 9) break;
            auto liveHeld = plr->supplies->inventory->getItem(liveSlot);
            if (!liveHeld || liveHeld->itemCount <= 0 || !liveHeld->block) break;

            candidates.clear();
            collectCandidates(plr, region, bridgeY, candidates);
            if (candidates.empty()) break;
        }

        bool placedThisPass = false;
        for (auto const& candidate : candidates) {
            if (candidate.face == FACE_NONE) continue;
            if (reinterpret_cast<bool (*)(SDK::GameMode*, BlockPos const*, uint8_t, bool)>(
                    Signatures::GameMode_buildBlock.result)(plr->gameMode, &candidate.support, candidate.face, true)) {
                placedThisPass = true;
                placedCount++;
                break;
            }
        }
        if (!placedThisPass) break;
    }
    bridging = false;

    if (placedCount == 0) return;

    auto delayMs = static_cast<int>(std::get<FloatValue>(bridgeDelay).value);
    nextPlace = now + std::chrono::milliseconds(std::max(0, delayMs));
}

bool LegitScaffold::shouldAllow(BlockPos const& blockPos, uint8_t face) const {
    auto ci = SDK::ClientInstance::get();
    auto plr = ci ? ci->getLocalPlayer() : nullptr;
    if (!plr) return true;

    if (face == FACE_DOWN) return false;
    if (face == FACE_UP) return std::get<BoolValue>(allowUp).value && isBelowFeet(blockPos);

    float nx = 0.f;
    float nz = 0.f;
    switch (face) {
    case FACE_NORTH:
        nz = -1.f;
        break;
    case FACE_SOUTH:
        nz = 1.f;
        break;
    case FACE_WEST:
        nx = -1.f;
        break;
    case FACE_EAST:
        nx = 1.f;
        break;
    default:
        return false;
    }

    Vec3 feet = feetPos(plr);
    float dx = feet.x - (static_cast<float>(blockPos.x) + 0.5f);
    float dz = feet.z - (static_cast<float>(blockPos.z) + 0.5f);

    return (nx * dx + nz * dz) >= -0.0001f;
}

bool LegitScaffold::isBelowFeet(BlockPos const& blockPos) const {
    auto ci = SDK::ClientInstance::get();
    auto plr = ci ? ci->getLocalPlayer() : nullptr;
    if (!plr) return false;

    Vec3 feet = feetPos(plr);

    if (floori(feet.x) != blockPos.x || floori(feet.z) != blockPos.z) return false;

    return blockPos.y < floori(feet.y + 0.05f);
}
