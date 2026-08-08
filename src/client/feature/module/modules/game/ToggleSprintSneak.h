#pragma once
#include "../../Module.h"

class ToggleSprintSneak : public Module {
public:
    ToggleSprintSneak();

    void onTick(Event& ev);
    void onKey(Event& ev);
    void onDisable() override;

private:
    void releaseForcedInput();

    ValueType sprint = BoolValue(true);
    ValueType alwaysSprint = BoolValue(false);
    ValueType sneak = BoolValue(false);

    bool toggleSprinting = false;
    bool toggleSneaking = false;
    bool lastSprintKey = false;
    bool lastSneakKey = false;
    bool realSneaking = false;
    bool realSprint = false;
    bool wasAlwaysSprint = false;
};
