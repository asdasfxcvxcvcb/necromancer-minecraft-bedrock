#pragma once
#include "../../Module.h"

class EnvironmentChanger : public Module {
public:
    EnvironmentChanger();

    void onWeather(Event&);
    void onTime(Event&);

private:
    ValueType setTime = BoolValue(false);
    ValueType time = FloatValue(0.f);
    ValueType showWeather = BoolValue(true);

    EnumData weatherMode;
};
