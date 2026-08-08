#include "pch.h"
#include "Hooks.h"

#include "hooks/GeneralHooks.h"
#include "hooks/LevelRendererHooks.h"
#include "hooks/OptionHooks.h"
#include "hooks/DXHooks.h"
#include "hooks/MinecraftGameHooks.h"
#include "hooks/RenderControllerHooks.h"
#include "hooks/ScreenViewHooks.h"
#include "hooks/PacketHooks.h"
#include "MinHook.h"
#include <vhook/vtable_hook.h>

using namespace std::chrono_literals;

NecromancerHooks::NecromancerHooks() {
}

NecromancerHooks::~NecromancerHooks() {
}

void NecromancerHooks::enable() {
    MH_EnableHook(MH_ALL_HOOKS);
}

MH_STATUS NecromancerHooks::disable() {
    const auto status = MH_DisableHook(MH_ALL_HOOKS);
    vh::unhook_all();
    vh::clear();
    return status;
}

void NecromancerHooks::releaseHookStorage() {
    forEach([](HookGroup& group) {
        std::erase_if(group.hooks, [](std::shared_ptr<Hook> const& hook) {
            return hook && !hook->isTableSwapHook();
        });
    });
}
