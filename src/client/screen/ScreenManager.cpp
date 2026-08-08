#include "pch.h"
#include "ScreenManager.h"
#include "screens/ClickGUI.h"
#include "screens/HUDEditor.h"
#include "mc/common/client/game/ClientInstance.h"
#include "client/event/events/KeyUpdateEvent.h"

ScreenManager::ScreenManager() {
    Eventing::get().listen<KeyUpdateEvent, &ScreenManager::onKey>(this, 3);
    Eventing::get().listen<FocusLostEvent, &ScreenManager::onFocusLost>(this);
    Eventing::get().listen<UpdateEvent, &ScreenManager::onUpdate>(this);
}

// releaseCursor()/grabCursor() reach into the active gameplay session's mouse
// state. In the main menu / title screen there's no local player and that state
// isn't set up, so calling them there dereferences uninitialized pointers and
// crashes. The cursor is already free in menus, so we simply skip the call when
// there's no local player.
static bool inGameplaySession() {
    auto client = SDK::ClientInstance::get();
    return client && client->getLocalPlayer();
}

void ScreenManager::activateScreen(Screen& screen, bool ignoreAnims) {
    if (this->activeScreen && &this->activeScreen->get() == &screen) {
        if (inGameplaySession()) SDK::ClientInstance::get()->releaseCursor();
        return;
    }

    if (this->activeScreen) {
        this->activeScreen->get().setActive(false);
    }

    this->activeScreen = screen;
    screen.setActive(true, ignoreAnims);
    if (inGameplaySession()) SDK::ClientInstance::get()->releaseCursor();
}

void ScreenManager::exitCurrentScreen() {
    if (this->activeScreen) {
        this->activeScreen->get().setActive(false);
        this->activeScreen = std::nullopt;
        if (inGameplaySession()) SDK::ClientInstance::get()->grabCursor();
    }
}

void ScreenManager::onKey(KeyUpdateEvent& ev) {
    if (ev.isDown() && ev.getKey() == VK_ESCAPE && getActiveScreen()) {
        exitCurrentScreen();
        ev.setCancelled(true);
        return;
    }

    std::optional<std::reference_wrapper<Screen>> associatedScreen;
    this->forEach([&](Screen& s) {
        if (s.key == ev.getKey() || (s.key2.value != 0 && s.key2 == ev.getKey())) associatedScreen = s;
    });

    // Open the menu when: playing normally (cursor grabbed), or sitting in the
    // main menu / title screen (a UI is showing but there's no local player yet).
    // The middle case — a UI while a local player exists (chat, inventory) — is
    // still excluded so typing 'm' in chat doesn't pop the menu open.
    auto client = SDK::ClientInstance::get();
    const bool inMainMenu = client && !client->getLocalPlayer();
    if (associatedScreen && ev.isDown() && (!ev.inUI() || inMainMenu || getActiveScreen())) {
        if (getActiveScreen()) {
            exitCurrentScreen();
        } else {
            activateScreen(associatedScreen->get());
        }
        ev.setCancelled(true);
        return;
    }
}

void ScreenManager::onFocusLost(FocusLostEvent& ev) {
    if (getActiveScreen()) {
        getActiveScreen()->get().resetInputState();
        if (inGameplaySession()) {
            SDK::ClientInstance::get()->releaseCursor();
        }
        ev.setCancelled(true);
    }
}

void ScreenManager::onUpdate(UpdateEvent&) {
    auto client = SDK::ClientInstance::get();
    if (getActiveScreen() && client && client->minecraftGame && client->minecraftGame->isCursorGrabbed()) {
        client->releaseCursor();
    }
}
