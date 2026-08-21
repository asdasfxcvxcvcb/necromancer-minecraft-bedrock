#pragma once
#include "client/feature/module/Module.h"

#include <chrono>

class Reach : public Module {
public:
    Reach();

    void onClick(Event& evG);
    void onAfterMove(Event& evG);
    void onDisable() override;

private:
    ValueType range = FloatValue(5.f);
    ValueType players = BoolValue(true);
    ValueType mobs = BoolValue(false);
    ValueType ignoreFriends = BoolValue(true);
    ValueType wallCheck = BoolValue(true);
    ValueType targetLagRecords = BoolValue(false);

    enum class TargetRecord {
        Live,
        Backtrack,
        AfterTrack,
    };

    struct PendingAttack {
        uint64_t runtimeID = 0;
        TargetRecord record = TargetRecord::Live;
        AABB box {};
        Vec3 hitPoint {};
        float recordAgeMs = -1.f;
        bool active = false;
    };

    PendingAttack pendingAttack {};

    bool pickTarget(SDK::LocalPlayer* lp, float maxRange, PendingAttack& out);
    class Backtrack* resolveBacktrack();
    class AfterTrack* resolveAfterTrack();

    Backtrack* backtrackModule = nullptr;
    bool backtrackResolved = false;
    AfterTrack* afterTrackModule = nullptr;
    bool afterTrackResolved = false;
};
