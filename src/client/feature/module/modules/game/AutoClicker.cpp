#include "pch.h"
#include "AutoClicker.h"

#include "client/event/events/UpdateEvent.h"
#include "client/misc/EntityCache.h"
#include "client/misc/PlayerListManager.h"

#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/world/actor/Actor.h"
#include "mc/common/world/actor/player/Player.h"
#include "mc/common/client/game/MouseDevice.h"
#include "mc/common/client/game/MouseAction.h"
#include "mc/common/client/player/LocalPlayer.h"
#include "mc/common/world/Minecraft.h"
#include "mc/common/world/actor/player/PlayerInventory.h"
#include "mc/common/world/actor/player/Inventory.h"
#include "mc/common/world/level/HitResult.h"
#include "mc/common/world/level/Level.h"
#include "mc/common/world/ItemStack.h"

AutoClicker::AutoClicker()
    : Module("AutoClicker", LocalizeString::get("client.module.autoClicker.name"),
             LocalizeString::get("client.module.autoClicker.desc"), GAME, nokeybind) {
    rng.seed(std::random_device{}());

    Setting::Condition leftFixedCond(std::vector<Setting::SingleCond> {
        { "leftClick", { 1 }, false },
        { "randomize", { 0 }, false },
    });
    Setting::Condition leftRangeCond(std::vector<Setting::SingleCond> {
        { "leftClick", { 1 }, false },
        { "randomize", { 1 }, false },
    });
    Setting::Condition rightFixedCond(std::vector<Setting::SingleCond> {
        { "rightClick", { 1 }, false },
        { "randomize", { 0 }, false },
    });
    Setting::Condition rightRangeCond(std::vector<Setting::SingleCond> {
        { "rightClick", { 1 }, false },
        { "randomize", { 1 }, false },
    });

    addSetting("leftClick", LocalizeString::get("client.module.autoClicker.leftClick.name"),
               LocalizeString::get("client.module.autoClicker.leftClick.desc"), leftClick);
    addSetting("blockBreak", LocalizeString::get("client.module.autoClicker.blockBreak.name"),
               LocalizeString::get("client.module.autoClicker.blockBreak.desc"), blockBreak, "leftClick"_istrue);
    addSetting("prioritizeAttack", LocalizeString::get("client.module.autoClicker.prioritizeAttack.name"),
               LocalizeString::get("client.module.autoClicker.prioritizeAttack.desc"), prioritizeAttack,
               Setting::Condition(std::vector<Setting::SingleCond> {
                   { "leftClick", { 1 }, false },
                   { "blockBreak", { 1 }, false },
               }));
    auto cpsLeftSet = addSliderSetting("cpsLeft", LocalizeString::get("client.module.autoClicker.cpsLeft.name"),
                     LocalizeString::get("client.module.autoClicker.cpsLeft.desc"), cpsLeft, FloatValue(1.f),
                     FloatValue(25.f), FloatValue(1.f), leftFixedCond);
    auto cpsLeftMinSet = addSliderSetting("cpsLeftMin", LocalizeString::get("client.module.autoClicker.cpsLeftMin.name"),
                     LocalizeString::get("client.module.autoClicker.cpsLeftMin.desc"), cpsLeftMin, FloatValue(1.f),
                     FloatValue(25.f), FloatValue(1.f), leftRangeCond);
    auto cpsLeftMaxSet = addSliderSetting("cpsLeftMax", LocalizeString::get("client.module.autoClicker.cpsLeftMax.name"),
                     LocalizeString::get("client.module.autoClicker.cpsLeftMax.desc"), cpsLeftMax, FloatValue(1.f),
                     FloatValue(25.f), FloatValue(1.f), leftRangeCond);

    addSetting("rightClick", LocalizeString::get("client.module.autoClicker.rightClick.name"),
               LocalizeString::get("client.module.autoClicker.rightClick.desc"), rightClick);
    addSetting("rightBlocksOnly", LocalizeString::get("client.module.autoClicker.rightBlocksOnly.name"),
               LocalizeString::get("client.module.autoClicker.rightBlocksOnly.desc"), rightBlocksOnly,
               "rightClick"_istrue);
    auto cpsRightSet = addSliderSetting("cpsRight", LocalizeString::get("client.module.autoClicker.cpsRight.name"),
                     LocalizeString::get("client.module.autoClicker.cpsRight.desc"), cpsRight, FloatValue(1.f),
                     FloatValue(25.f), FloatValue(1.f), rightFixedCond);
    auto cpsRightMinSet = addSliderSetting("cpsRightMin", LocalizeString::get("client.module.autoClicker.cpsRightMin.name"),
                     LocalizeString::get("client.module.autoClicker.cpsRightMin.desc"), cpsRightMin, FloatValue(1.f),
                     FloatValue(25.f), FloatValue(1.f), rightRangeCond);
    auto cpsRightMaxSet = addSliderSetting("cpsRightMax", LocalizeString::get("client.module.autoClicker.cpsRightMax.name"),
                     LocalizeString::get("client.module.autoClicker.cpsRightMax.desc"), cpsRightMax, FloatValue(1.f),
                     FloatValue(25.f), FloatValue(1.f), rightRangeCond);

    addSetting("randomize", LocalizeString::get("client.module.autoClicker.randomize.name"),
               LocalizeString::get("client.module.autoClicker.randomize.desc"), randomize);

    cpsLeftSet->floatEditMax = 10000.f;
    cpsLeftMinSet->floatEditMax = 10000.f;
    cpsLeftMaxSet->floatEditMax = 10000.f;
    cpsRightSet->floatEditMax = 10000.f;
    cpsRightMinSet->floatEditMax = 10000.f;
    cpsRightMaxSet->floatEditMax = 10000.f;

    cpsLeftMinSet->userUpdateCallback = [this](Setting&) {
        auto& mn = std::get<FloatValue>(cpsLeftMin).value;
        auto& mx = std::get<FloatValue>(cpsLeftMax).value;
        if (mn > mx) mn = mx;
    };
    cpsLeftMaxSet->userUpdateCallback = [this](Setting&) {
        auto& mn = std::get<FloatValue>(cpsLeftMin).value;
        auto& mx = std::get<FloatValue>(cpsLeftMax).value;
        if (mx < mn) mx = mn;
    };
    cpsRightMinSet->userUpdateCallback = [this](Setting&) {
        auto& mn = std::get<FloatValue>(cpsRightMin).value;
        auto& mx = std::get<FloatValue>(cpsRightMax).value;
        if (mn > mx) mn = mx;
    };
    cpsRightMaxSet->userUpdateCallback = [this](Setting&) {
        auto& mn = std::get<FloatValue>(cpsRightMin).value;
        auto& mx = std::get<FloatValue>(cpsRightMax).value;
        if (mx < mn) mx = mn;
    };

    this->listen<UpdateEvent>(&AutoClicker::onUpdate);
}

float AutoClicker::sampleCps(float fixed, float minVal, float maxVal) {
    if (!std::get<BoolValue>(randomize)) return fixed;
    if (minVal > maxVal) std::swap(minVal, maxVal);
    std::uniform_real_distribution<float> dist(minVal, maxVal);
    return dist(rng);
}

void AutoClicker::afterLoadConfig() {
    auto clampCps = [](ValueType& val) {
        float& cps = std::get<FloatValue>(val).value;
        if (cps < 1.f) cps = 1.f;
    };

    clampCps(cpsLeft);
    clampCps(cpsLeftMin);
    clampCps(cpsLeftMax);
    clampCps(cpsRight);
    clampCps(cpsRightMin);
    clampCps(cpsRightMax);

    auto& leftMin = std::get<FloatValue>(cpsLeftMin).value;
    auto& leftMax = std::get<FloatValue>(cpsLeftMax).value;
    if (leftMin > leftMax) std::swap(leftMin, leftMax);

    auto& rightMin = std::get<FloatValue>(cpsRightMin).value;
    auto& rightMax = std::get<FloatValue>(cpsRightMax).value;
    if (rightMin > rightMax) std::swap(rightMin, rightMax);
}

void AutoClicker::pushAction(int button, bool down) {
    auto mouse = SDK::MouseDevice::get();
    if (!mouse) return;

    SDK::MouseAction action {};
    action.x = mouse->x;
    action.y = mouse->y;
    action.dx = 0;
    action.dy = 0;
    action.action = static_cast<int8_t>(button);
    action.data = down ? int8_t { 1 } : int8_t { 0 };
    action.pointerId = 0;
    action.forceMotionlessPointer = false;
    mouse->inputs.push_back(action);
}

bool AutoClicker::isHoldingBlock() {
    auto clientInstance = SDK::ClientInstance::get();
    auto localPlayer = clientInstance ? clientInstance->getLocalPlayer() : nullptr;
    if (!localPlayer || !localPlayer->supplies || !localPlayer->supplies->inventory) return false;

    auto stack = localPlayer->supplies->inventory->getItem(localPlayer->supplies->selectedSlot);
    return stack && stack->block != nullptr;
}

bool AutoClicker::hasAttackableTarget() {
    auto clientInstance = SDK::ClientInstance::get();
    if (!clientInstance || !clientInstance->minecraft) return false;
    auto level = clientInstance->minecraft->getLevel();
    if (!level) return false;
    auto lp = clientInstance->getLocalPlayer();
    if (!lp) return false;

    auto hit = level->getHitResult();
    if (!hit) return false;

    Vec3 direction = hit->end.magnitude() > 0.0001f ? hit->end.normalized() : Vec3 { 0.f, 0.f, 0.f };
    if (direction.magnitude() <= 0.0001f) return false;

    float nearest = 6.f;
    auto snap = EntityCache::get().snapshot();
    for (auto const& view : snap->views) {
        SDK::Actor* entt = view.actor;
        if (!entt || entt == lp || !entt->aabbShape) continue;
        if (view.isItem || !view.hasHealth || view.health <= 0.f) continue;
        if (view.invisible) continue;
        if (view.isPlayer && PlayerListManager::get().isFriend(reinterpret_cast<SDK::Player*>(entt)->playerName))
            continue;

        auto hitDist = entt->getBoundingBox().intersectsRay(hit->start, direction, nearest, 0.08f);
        if (hitDist && *hitDist < nearest) return true;
    }

    return false;
}

bool AutoClicker::isPhysicallyHeld(int button) {
    int vk = button == 1 ? VK_LBUTTON : VK_RBUTTON;
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool AutoClicker::isAimingAtBlock() {
    auto clientInstance = SDK::ClientInstance::get();
    if (!clientInstance || !clientInstance->minecraft) return false;
    auto level = clientInstance->minecraft->getLevel();
    if (!level) return false;

    auto hit = level->getHitResult();
    return hit && hit->hitType == SDK::HitType::BLOCK;
}

void AutoClicker::handleButton(int button, bool btnEnabled, float cpsFixed, float cpsMin, float cpsMax,
                               std::chrono::steady_clock::time_point& nextClick,
                               std::chrono::steady_clock::time_point now) {
    bool physicallyHeld = isPhysicallyHeld(button);

    if (button == 1) {
        if (!btnEnabled || !physicallyHeld) {
            if (leftBlockHeldByUs) {
                pushAction(1, false);
                leftBlockHeldByUs = false;
            }
            nextClick = now;
            return;
        }

        if (std::get<BoolValue>(blockBreak) && isAimingAtBlock() &&
            !(std::get<BoolValue>(prioritizeAttack) && hasAttackableTarget())) {
            if (!leftBlockHeldByUs) {
                pushAction(1, true);
                leftBlockHeldByUs = true;
            }
            nextClick = now;
            return;
        }

        if (leftBlockHeldByUs) {
            pushAction(1, false);
            leftBlockHeldByUs = false;
            nextClick = now;
            return;
        }
    }

    if (!btnEnabled) {
        nextClick = now;
        return;
    }

    if (!physicallyHeld) {
        nextClick = now;
        return;
    }

    if (button == 2 && std::get<BoolValue>(rightBlocksOnly) && !isHoldingBlock()) {
        nextClick = now;
        return;
    }

    if (now >= nextClick) {
        if (nextClick < now - std::chrono::milliseconds(100)) nextClick = now;

        constexpr int maxBurst = 512;
        if (auto mouse = SDK::MouseDevice::get()) {
            mouse->inputs.reserve(mouse->inputs.size() + maxBurst * 2);
        }

        int clicks = 0;
        while (now >= nextClick && clicks < maxBurst) {
            pushAction(button, true);
            pushAction(button, false);

            float cps = std::max(sampleCps(cpsFixed, cpsMin, cpsMax), 1.f);
            nextClick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<float>(1.f / cps));
            ++clicks;
        }
        if (clicks >= maxBurst) nextClick = now;
    }
}

void AutoClicker::onUpdate(Event&) {
    auto clientInstance = SDK::ClientInstance::get();
    if (!clientInstance || !clientInstance->minecraftGame) return;

    if (!clientInstance->minecraftGame->isCursorGrabbed()) {
        if (leftBlockHeldByUs) {
            pushAction(1, false);
            leftBlockHeldByUs = false;
        }
        return;
    }

    auto now = std::chrono::steady_clock::now();

    handleButton(1, std::get<BoolValue>(leftClick), std::get<FloatValue>(cpsLeft).value,
                 std::get<FloatValue>(cpsLeftMin).value, std::get<FloatValue>(cpsLeftMax).value, nextLeftClick, now);

    handleButton(2, std::get<BoolValue>(rightClick), std::get<FloatValue>(cpsRight).value,
                 std::get<FloatValue>(cpsRightMin).value, std::get<FloatValue>(cpsRightMax).value, nextRightClick,
                 now);
}

void AutoClicker::onDisable() {
    if (leftBlockHeldByUs) {
        pushAction(1, false);
        leftBlockHeldByUs = false;
    }
}
