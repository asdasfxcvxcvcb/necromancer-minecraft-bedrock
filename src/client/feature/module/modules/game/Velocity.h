#pragma once
#include "../../Module.h"

class Velocity : public Module {
public:
    Velocity();

    void onPacketReceive(Event& evG);

private:
    ValueType intensity = FloatValue(100.f);
};
