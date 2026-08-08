#pragma once
#include "screens/ClickGUI.h"
#include "screens/HUDEditor.h"
#include "screens/SkinStealerScreen.h"

#include "client/manager/StaticManager.h"
#include "client/event/Listener.h"
#include "client/event/events/UpdateEvent.h"
#include "Screen.h"

#include "util/Util.h"

class ScreenManager : public Listener, public StaticManager<Screen, ClickGUI, HUDEditor, SkinStealerScreen> {
public:
    ScreenManager();

    template<typename T>
    void showScreen(bool ignoreAnims = false) {
        auto& scr = std::get<T>(items);

        activateScreen(scr, ignoreAnims);
    }

    template<typename T>
    bool tryToggleScreen() {
        auto& screen = std::get<T>(items);
        if (activeScreen && activeScreen->get().getName() == screen.getName()) {
            this->exitCurrentScreen();
            return true;
        }

        showScreen<T>();
        return true;
    }
    void exitCurrentScreen();

    [[nodiscard]] std::optional<std::reference_wrapper<Screen>> getActiveScreen() { return activeScreen; };

    void onKey(KeyUpdateEvent& ev);
    void onFocusLost(FocusLostEvent& ev);
    void onUpdate(UpdateEvent& ev);

private:
    void activateScreen(Screen& screen, bool ignoreAnims = false);
    std::optional<std::reference_wrapper<Screen>> activeScreen;
};
