#pragma once
#include "client/event/Event.h"
#include "util/Crypto.h"

class WeatherEvent : public Event {
public:
    static const uint32_t hash = TOHASH(WeatherEvent);

    WeatherEvent() {}

    void setShowWeather(bool showWeather) { this->showWeather = showWeather; }

    bool shouldShowWeather() { return showWeather; }

    void setWeatherMode(int mode) { this->weatherMode = mode; }

    int getWeatherMode() { return weatherMode; }

protected:
    bool showWeather = true;
    int weatherMode = 0;
};
