#include "pch.h"
#include "EnvironmentChanger.h"
#include <client/event/events/WeatherEvent.h>

EnvironmentChanger::EnvironmentChanger()
    : Module("EnvironmentChanger", LocalizeString::get("client.module.environmentChanger.name"),
             LocalizeString::get("client.module.environmentChanger.desc"), GAME) {
    listen<WeatherEvent>(static_cast<EventListenerFunc>(&EnvironmentChanger::onWeather));
    listen<GetTimeEvent>(static_cast<EventListenerFunc>(&EnvironmentChanger::onTime));

    weatherMode.addEntry(EnumEntry(0, LocalizeString::get("client.module.environmentChanger.weatherMode.default.name"),
                                   LocalizeString::get("client.module.environmentChanger.weatherMode.default.desc")));
    weatherMode.addEntry(EnumEntry(1, LocalizeString::get("client.module.environmentChanger.weatherMode.clear.name"),
                                   LocalizeString::get("client.module.environmentChanger.weatherMode.clear.desc")));
    weatherMode.addEntry(EnumEntry(2, LocalizeString::get("client.module.environmentChanger.weatherMode.rain.name"),
                                   LocalizeString::get("client.module.environmentChanger.weatherMode.rain.desc")));
    weatherMode.addEntry(EnumEntry(3, LocalizeString::get("client.module.environmentChanger.weatherMode.thunder.name"),
                                   LocalizeString::get("client.module.environmentChanger.weatherMode.thunder.desc")));

    addSetting("setTime", LocalizeString::get("client.module.environmentChanger.setTime.name"),
               LocalizeString::get("client.module.environmentChanger.setTime.desc"), setTime);

    addSliderSetting("timeToSet", LocalizeString::get("client.module.environmentChanger.timeToSet.name"),
                     LocalizeString::get("client.module.environmentChanger.timeToSet.desc"), time, FloatValue(0.f),
                     FloatValue(1.f), FloatValue(0.01f), "setTime");

    addSetting("showWeather", LocalizeString::get("client.module.environmentChanger.showWeather.name"),
               LocalizeString::get("client.module.environmentChanger.showWeather.desc"), showWeather);

    addEnumSetting("weatherMode", LocalizeString::get("client.module.environmentChanger.weatherMode.name"),
                   LocalizeString::get("client.module.environmentChanger.weatherMode.desc"), weatherMode,
                   "showWeather"_istrue);
}

void EnvironmentChanger::onWeather(Event& evG) {
    auto& ev = reinterpret_cast<WeatherEvent&>(evG);

    if (!std::get<BoolValue>(showWeather)) {
        ev.setShowWeather(false);
        return;
    }

    ev.setWeatherMode(weatherMode.getSelectedKey());
}

void EnvironmentChanger::onTime(Event& evG) {
    auto& ev = reinterpret_cast<GetTimeEvent&>(evG);

    if (std::get<BoolValue>(setTime)) {
        ev.setTime(std::get<FloatValue>(time));
    }
}
