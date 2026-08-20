#pragma once
#include "../../Module.h"

class GameTimer : public Module {
public:
    GameTimer();

    void onEnable() override;
    void onDisable() override;
    void onUpdate(Event& ev);

private:
    ValueType ticks = FloatValue(20.f);

    float defaultTps = 20.f;
    bool loggedFields = false;
};
