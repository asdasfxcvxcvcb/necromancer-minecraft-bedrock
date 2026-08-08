#include "pch.h"
#include "DisableMouseWheel.h"
#include <client/event/events/ClickEvent.h>
#include <client/screen/ScreenManager.h>
#include <mc/common/client/game/MinecraftGame.h>

DisableMouseWheel::DisableMouseWheel()
    : Module("DisableMouseWheel", LocalizeString::get("client.module.disableMouseWheel.name"),
             LocalizeString::get("client.module.disableMouseWheel.desc"), GAME) {
    this->listen<MouseWheelEvent>(&DisableMouseWheel::onMouseWheel);
    this->listen<ClickEvent>(&DisableMouseWheel::onClick, false, 5);
}

bool DisableMouseWheel::shouldBlock() const {
    if (Necromancer::get().getScreenManager().getActiveScreen().has_value()) return false;

    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraftGame) return false;
    return ci->minecraftGame->isCursorGrabbed();
}

void DisableMouseWheel::onMouseWheel(Event& evG) {
    if (!shouldBlock()) return;
    reinterpret_cast<MouseWheelEvent&>(evG).setCancelled();
}

void DisableMouseWheel::onClick(Event& evG) {
    auto& ev = reinterpret_cast<ClickEvent&>(evG);
    if (ev.getClickType() != ClickEvent::ClickType::Wheel || !shouldBlock()) return;
    ev.setCancelled();
}
