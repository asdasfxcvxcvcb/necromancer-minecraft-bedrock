#include "pch.h"
#include "GameTimer.h"
#include "client/event/events/UpdateEvent.h"
#include "util/Logger.h"
#include <mc/common/client/game/ClientInstance.h>
#include <mc/common/world/Minecraft.h>
#include <mc/common/world/Timer.h>

GameTimer::GameTimer()
    : Module("Timer", LocalizeString::get("client.module.timer.name"),
             LocalizeString::get("client.module.timer.desc"), GAME) {
    auto ticksSet = addSliderSetting("ticks", LocalizeString::get("client.module.timer.ticks.name"),
                                     LocalizeString::get("client.module.timer.ticks.desc"), ticks, FloatValue(1.f),
                                     FloatValue(100.f), FloatValue(1.f));
    ticksSet->floatEditMax = 10000.f;

    this->listen<UpdateEvent>(&GameTimer::onUpdate);
}

void GameTimer::onEnable() {
    defaultTps = 20.f;
    loggedFields = false;
}

void GameTimer::onDisable() {
    auto ci = SDK::ClientInstance::get();
    if (ci && ci->minecraft && ci->minecraft->timer) ci->minecraft->timer->tps = defaultTps;
}

void GameTimer::onUpdate(Event&) {
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft || !ci->minecraft->timer) return;

    auto* timer = ci->minecraft->timer;
    if (!loggedFields) {
        loggedFields = true;
        defaultTps = timer->tps;
        Logger::Info("[Timer] fields: tps={} ticks={} alpha={} timeScale={} passedTime={}", timer->tps, timer->ticks,
                     timer->alpha, timer->timeScale, timer->passedTime);
    }

    timer->tps = std::get<FloatValue>(ticks).value;
}
