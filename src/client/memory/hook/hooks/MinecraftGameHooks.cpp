#include "pch.h"
#include "MinecraftGameHooks.h"
#include "client/event/events/AppSuspendedEvent.h"
#include "client/event/events/UpdateEvent.h"
#include "client/event/Eventing.h"
#include "client/Necromancer.h"

namespace {
    std::shared_ptr<Hook> onDeviceLostHook;
    std::shared_ptr<Hook> _updateHook;

    void __fastcall updateImpl(SDK::MinecraftGame* game) {
        BEGIN_ERROR_HANDLER
        _updateHook->oFunc<decltype(&updateImpl)>()(game);
        UpdateEvent ev {};

        Eventing::get().dispatch(ev);
        END_ERROR_HANDLER
    }

#ifdef NECROMANCER_CRASH_REPORTING
    void __cdecl updateSehThunk(void* context) {
        updateImpl(static_cast<SDK::MinecraftGame*>(context));
    }
#endif
}

void MinecraftGameHooks::onDeviceLost(SDK::MinecraftGame* game) {
    FocusLostEvent ev {};

    if (Eventing::get().dispatch(ev)) return;

    onDeviceLostHook->oFunc<decltype(&onDeviceLost)>()(game);
}

void __fastcall MinecraftGameHooks::_update(SDK::MinecraftGame* game) {
#ifdef NECROMANCER_CRASH_REPORTING
    DebugExceptionHandler::RunVoidWithSehGuard(updateSehThunk, game,
                                               "Caught SEH exception in MinecraftGame::_update hook");
#else
    updateImpl(game);
#endif
}

MinecraftGameHooks::MinecraftGameHooks() {
    onDeviceLostHook =
        addHook(Signatures::MinecraftGame_onDeviceLost.result, onDeviceLost, "MinecraftGame::onDeviceLost");
    _updateHook = addHook(Signatures::MinecraftGame__update.result, _update, "MinecraftGame::_update");
}
