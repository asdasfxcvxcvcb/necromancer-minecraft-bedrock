#pragma once

#include "../Hook.h"

namespace SDK {
    class GameMode;
}

class PlayerHooks : public HookGroup {
private:
    static void* hkActorAttack(SDK::Actor* obj, void* ret, SDK::Actor* target, void* cause, void* a4);
    static bool hkGameModeBuildBlock(SDK::GameMode* gameMode, BlockPos const* blockPos, uint8_t face, bool a4);

public:
    PlayerHooks();
};
