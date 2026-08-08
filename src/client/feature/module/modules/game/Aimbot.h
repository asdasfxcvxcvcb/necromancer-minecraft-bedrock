#pragma once
#include "client/feature/module/Module.h"
#include "util/LMath.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

namespace SDK {
    class Actor;
}

class Aimbot : public Module {
public:
    Aimbot();

    void onUpdate(Event& evG);
    void onCameraUpdate(Event& evG);
    void onTurnDelta(Event& evG);
    void onCinematicCamera(Event& evG);
    void onRenderOverlay(RenderOverlayEvent& ev);
    void onRendererCleanup(Event& evG);
    void onDisable() override;
    void afterLoadConfig() override;

private:
    ValueType players = BoolValue(true);
    ValueType mobs = BoolValue(false);
    ValueType prioritizeTags = BoolValue(true);
    ValueType ignoreFriends = BoolValue(true);
    ValueType wallCheck = BoolValue(true);
    ValueType backtrackTarget = BoolValue(false);
    ValueType aimDrift = FloatValue(0.f);

    ValueType smoothSpeed = FloatValue(8.f);
    ValueType lockOn = BoolValue(false);

    ValueType range = FloatValue(5.f);
    EnumData targetMode;
    EnumData hitbox;
    ValueType fov = FloatValue(90.f);
    ValueType fovColor = ColorValue(1.f, 1.f, 1.f, 0.75f);
    ValueType fovWidth = FloatValue(1.5f);

    struct TargetCandidate {
        SDK::Actor* actor;
        float score;
        int priority;
        bool retained;
        bool isGhost;
        AABB bounds;
        Vec3 frac;
    };

    std::vector<TargetCandidate> candidates;
    class Freelook* freelookModule = nullptr;
    bool freelookResolved = false;
    uint64_t currentTargetId = 0;
    bool currentTargetGhost = false;
    Vec3 currentAimFrac { 0.5f, 0.55f, 0.5f };
    bool haveCurrentAimFrac = false;
    uint64_t desiredTargetId = 0;
    Vec3 desiredAimFrac { 0.5f, 0.55f, 0.5f };
    bool desiredIsGhost = false;
    AABB desiredGhostBox {};
    bool desiredDriftReset = false;
    bool commandActive = false;
    Vec2 commandedRot {};
    Vec2 pendingTurnDelta {};
    std::atomic<float> userInput = 0.f;
    std::chrono::steady_clock::time_point lastFrame {};
    uint64_t lastCorrectionFrame = UINT64_MAX;
    std::atomic_bool injectingTurn = false;
    std::recursive_mutex controllerMutex;
    std::mutex aimMutex;
    float driftPhase = 0.55f;
    int driftDir = 1;
    std::chrono::steady_clock::time_point lastDriftTick {};
    class Backtrack* backtrackModule = nullptr;
    bool backtrackResolved = false;
    ComPtr<ID2D1SolidColorBrush> ringBrush;
};
