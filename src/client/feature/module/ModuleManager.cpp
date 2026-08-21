#include "pch.h"
#include "ModuleManager.h"
#include "modules/misc/TestModule.h"
#include "modules/misc/Nickname.h"
#include "modules/misc/ItemTweaks.h"
#include "modules/misc/SkinStealer.h"
#include "modules/misc/Font.h"
#include "modules/misc/AntiBot.h"
#include "modules/misc/DisableMouseWheel.h"
#include "modules/misc/ChatSpammer.h"
#include "modules/misc/AutoBlock.h"
#include "modules/misc/LegitScaffold.h"
#include "modules/misc/ChestStealer.h"
#include "modules/misc/ItemSwitcher.h"
#include "modules/misc/DebugInfo.h"

#include "modules/game/Zoom.h"
#include "modules/game/ToggleSprintSneak.h"
#include "modules/game/ThirdPersonNametag.h"
#include "modules/game/EnvironmentChanger.h"
#include "modules/game/TextHotkey.h"
#include "modules/game/Freelook.h"
#include "modules/game/KillNotification.h"
#include "modules/game/AutoClicker.h"
#include "modules/game/Aimbot.h"
#include "modules/game/Triggerbot.h"
#include "modules/game/ShieldBreaker.h"
#include "modules/game/Reach.h"
#include "modules/game/NoFall.h"
#include "modules/game/Fakelag.h"
#include "modules/game/Backtrack.h"
#include "modules/game/AfterTrack.h"
#include "modules/game/Velocity.h"
#include "modules/game/AntiAFK.h"
#include "modules/game/GameTimer.h"

#include "modules/visual/Fullbright.h"
#include "modules/visual/HurtColor.h"
#include "modules/visual/Hitboxes.h"
#include "modules/visual/BlockOutline.h"
#include "modules/visual/BlockESP.h"
#include "modules/visual/ESP.h"
#include "modules/visual/OutOfViewArrows.h"
#include "modules/visual/AntiObs.h"
#include "modules/visual/DamageIndicator.h"

#include "modules/hud/FPSCounter.h"
#include "modules/hud/CPSCounter.h"
#include "modules/hud/ServerDisplay.h"
#include "modules/hud/PingDisplay.h"
#include "modules/hud/SpeedDisplay.h"
#include "modules/hud/BowIndicator.h"
#include "modules/hud/TabList.h"
#include "modules/hud/Keystrokes.h"
#include "modules/hud/KeybindList.h"
#include "modules/hud/BreakIndicator.h"
#include "modules/hud/ItemCounter.h"
#include "modules/hud/ArmorHud.h"
#include "modules/hud/Chat.h"
#include "modules/hud/ComboCounter.h"
#include "modules/hud/CustomCoordinates.h"
#include "modules/hud/MovableCoordinates.h"
#include "modules/hud/WAILA.h"

#include "client/event/events/KeyUpdateEvent.h"

ModuleManager::ModuleManager() {
#ifdef NECROMANCER_DEBUG
    this->items.push_back(std::make_shared<TestModule>());
#endif
    this->items.push_back(std::make_shared<Zoom>());
    this->items.push_back(std::make_shared<Fullbright>());
    // this->items.push_back(std::make_shared<HurtColor>());
    this->items.push_back(std::make_shared<FPSCounter>());
    this->items.push_back(std::make_shared<CPSCounter>());
    this->items.push_back(std::make_shared<ServerDisplay>());
    this->items.push_back(std::make_shared<PingDisplay>());
    this->items.push_back(std::make_shared<SpeedDisplay>());
    this->items.push_back(std::make_shared<ToggleSprintSneak>());
    this->items.push_back(std::make_shared<BowIndicator>());
    this->items.push_back(std::make_shared<TabList>());
    this->items.push_back(std::make_shared<Keystrokes>());
    this->items.push_back(std::make_shared<KeybindList>());
    this->items.push_back(std::make_shared<BreakIndicator>());
    this->items.push_back(std::make_shared<Hitboxes>());
    this->items.push_back(std::make_shared<BlockOutline>());
    this->items.push_back(std::make_shared<BlockESP>());
    this->items.push_back(std::make_shared<Nickname>());
    this->items.push_back(std::make_shared<ThirdPersonNametag>());
    this->items.push_back(std::make_shared<EnvironmentChanger>());
    this->items.push_back(std::make_shared<ItemCounter>());
    this->items.push_back(std::make_shared<ArmorHud>());
    // this->items.push_back(std::make_shared<Chat>());
    this->items.push_back(std::make_shared<TextHotkey>());
    this->items.push_back(std::make_shared<Freelook>());
    this->items.push_back(std::make_shared<ComboCounter>());
    this->items.push_back(std::make_shared<CustomCoordinates>());
    this->items.push_back(std::make_shared<MovableCoordinates>());
    this->items.push_back(std::make_shared<WAILA>());
    this->items.push_back(std::make_shared<Font>());
    this->items.push_back(std::make_shared<SkinStealer>());
    this->items.push_back(std::make_shared<KillNotification>());
    this->items.push_back(std::make_shared<AutoClicker>());
    this->items.push_back(std::make_shared<Aimbot>());
    this->items.push_back(std::make_shared<Triggerbot>());
    this->items.push_back(std::make_shared<ShieldBreaker>());
    this->items.push_back(std::make_shared<Reach>());
    this->items.push_back(std::make_shared<NoFall>());
    this->items.push_back(std::make_shared<Fakelag>());
    this->items.push_back(std::make_shared<Backtrack>());
    this->items.push_back(std::make_shared<AfterTrack>());
    this->items.push_back(std::make_shared<Velocity>());
    this->items.push_back(std::make_shared<AntiAFK>());
    this->items.push_back(std::make_shared<GameTimer>());
    this->items.push_back(std::make_shared<ESP>());
    this->items.push_back(std::make_shared<OutOfViewArrows>());
    this->items.push_back(std::make_shared<AntiObs>());
    this->items.push_back(std::make_shared<DamageIndicator>());
    this->items.push_back(std::make_shared<AntiBot>());
    this->items.push_back(std::make_shared<DisableMouseWheel>());
    this->items.push_back(std::make_shared<ChatSpammer>());
    this->items.push_back(std::make_shared<AutoBlock>());
    this->items.push_back(std::make_shared<LegitScaffold>());
    this->items.push_back(std::make_shared<ChestStealer>());
    this->items.push_back(std::make_shared<ItemSwitcher>());
    this->items.push_back(std::make_shared<DebugInfo>());

    for (auto& mod : items) {
        mod->onInit();
    }
    Eventing::get().listen<KeyUpdateEvent>(this, (EventListenerFunc)&ModuleManager::onKey);
}

ModuleManager::~ModuleManager() {
    for (auto& mod : items) {
        if (mod->isEnabled()) mod->setEnabled(false);
    }
}

void ModuleManager::onKey(Event& evGeneric) {
    auto& ev = reinterpret_cast<KeyUpdateEvent&>(evGeneric);
    for (auto& mod : items) {
        if (ev.inUI()) return;
        if (mod->getKeybind() == ev.getKey()) {
            if (mod->shouldHoldToToggle()) {
                if (!mod->isEnabled() && ev.isDown()) {
                    mod->setEnabled(true);
                } else if (mod->isEnabled() && !ev.isDown()) {
                    mod->setEnabled(false);
                }
                continue;
            } else if (ev.isDown()) {
                mod->setEnabled(!mod->isEnabled());
            }
        }
    }
}
