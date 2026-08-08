#include "pch.h"
#include "ToggleSprintSneak.h"
#include "client/event/events/TickEvent.h"
#include "client/event/events/KeyUpdateEvent.h"
#include "mc/common/client/input/ClientInputHandler.h"

ToggleSprintSneak::ToggleSprintSneak()
    : Module("ToggleSprintSneak", LocalizeString::get("client.textmodule.toggleSprintSneak.name"),
             LocalizeString::get("client.textmodule.toggleSprintSneak.desc"), GAME) {
    listen<TickEvent>(static_cast<EventListenerFunc>(&ToggleSprintSneak::onTick));
    listen<KeyUpdateEvent>(static_cast<EventListenerFunc>(&ToggleSprintSneak::onKey));

    addSetting("toggleSprint", LocalizeString::get("client.textmodule.toggleSprintSneak.toggleSprint.name"),
               LocalizeString::get("client.textmodule.toggleSprintSneak.toggleSprint.desc"), sprint);
    addSetting("alwaysSprint", LocalizeString::get("client.textmodule.toggleSprintSneak.alwaysSprint.name"),
               LocalizeString::get("client.textmodule.toggleSprintSneak.alwaysSprint.desc"), alwaysSprint,
               "toggleSprint"_istrue);
    addSetting("toggleSneak", LocalizeString::get("client.textmodule.toggleSprintSneak.toggleSneak.name"),
               LocalizeString::get("client.textmodule.toggleSprintSneak.toggleSneak.desc"), sneak);
}

void ToggleSprintSneak::releaseForcedInput() {
    auto ci = SDK::ClientInstance::get();
    auto plr = ci ? ci->getLocalPlayer() : nullptr;
    if (!plr) return;
    auto input = plr->getMoveInputComponent();
    if (!input) return;

    input->rawInputState.sprintDown = false;
    input->inputState.sprintDown = false;
    input->rawInputState.sneakDown = false;
    input->inputState.sneakDown = false;
    if (!realSprint) plr->setSprinting(false);
}

void ToggleSprintSneak::onTick(Event&) {
    auto ci = SDK::ClientInstance::get();
    auto plr = ci ? ci->getLocalPlayer() : nullptr;
    if (!plr) return;
    auto input = plr->getMoveInputComponent();
    if (!input) return;

    bool sprintEnabled = std::get<BoolValue>(sprint);
    bool sneakEnabled = std::get<BoolValue>(sneak);
    bool alwaysSprintEnabled = sprintEnabled && std::get<BoolValue>(alwaysSprint);
    if (!sprintEnabled || (wasAlwaysSprint && !alwaysSprintEnabled)) toggleSprinting = false;
    if (!sneakEnabled) toggleSneaking = false;
    if (sprintEnabled && realSprint && !lastSprintKey) toggleSprinting = !toggleSprinting;
    if (sneakEnabled && realSneaking && !lastSneakKey) toggleSneaking = !toggleSneaking;
    if (alwaysSprintEnabled) toggleSprinting = true;
    wasAlwaysSprint = alwaysSprintEnabled;

    if (sprintEnabled) {
        if (toggleSprinting) {
            input->rawInputState.sprintDown = true;
            input->inputState.sprintDown = true;
            input->sprinting = true;
        } else {
            input->rawInputState.sprintDown = false;
            input->inputState.sprintDown = false;
            if (!realSprint) plr->setSprinting(false);
        }
    }

    if (sneakEnabled) {
        if (toggleSneaking) {
            input->rawInputState.sneakDown = true;
            input->inputState.sneakDown = true;
        } else {
            input->rawInputState.sneakDown = false;
            input->inputState.sneakDown = false;
        }
    }

    lastSprintKey = realSprint;
    lastSneakKey = realSneaking;
}

void ToggleSprintSneak::onKey(Event& evGeneric) {
    auto& ev = reinterpret_cast<KeyUpdateEvent&>(evGeneric);
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->inputHandler || !ci->inputHandler->mappingFactory ||
        !ci->inputHandler->mappingFactory->defaultKeyboardLayout) {
        return;
    }

    auto* layout = ci->inputHandler->mappingFactory->defaultKeyboardLayout;

    if (ev.getKey() == layout->findValue("sprint")) {
        realSprint = ev.isDown();
    }
    if (ev.getKey() == layout->findValue("sneak")) {
        realSneaking = ev.isDown();
    }
}

void ToggleSprintSneak::onDisable() {
    toggleSprinting = false;
    toggleSneaking = false;
    lastSprintKey = false;
    lastSneakKey = false;
    wasAlwaysSprint = false;
    releaseForcedInput();
}
