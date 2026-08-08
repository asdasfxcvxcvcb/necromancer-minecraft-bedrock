#pragma once
#include "client/feature/module/Module.h"
#include "util/LMath.h"

class DamageIndicator : public Module {
public:
    DamageIndicator();

    void onAttack(Event& evG);
    void onTick(Event& evG);
    void onRenderLayer(RenderLayerEvent& ev);

private:
    struct TrackedTarget {
        float lastHealth = 0.f;
        std::chrono::steady_clock::time_point lastAttack {};
    };

    struct DamagePopup {
        Vec3 pos;
        float amount = 0.f;
        std::chrono::steady_clock::time_point spawnTime {};
    };

    void spawnPopup(SDK::Actor* actor, float amount);

    ValueType textSize = FloatValue(18.f);
    ValueType textColor = ColorValue(1.f, 0.55f, 0.1f, 1.f);

    std::unordered_map<uint64_t, TrackedTarget> tracked;
    std::vector<DamagePopup> popups;
};
