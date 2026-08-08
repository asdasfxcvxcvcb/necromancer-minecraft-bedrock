#include "pch.h"
#include "PlayerHooks.h"
#include "client/event/events/AttackEvent.h"
#include "client/event/events/BuildBlockEvent.h"
#include "mc/common/world/actor/player/GameMode.h"

static std::shared_ptr<Hook> ActorAttackHook;
static std::shared_ptr<Hook> GameModeBuildBlockHook;

void* PlayerHooks::hkActorAttack(SDK::Actor* obj, void* ret, SDK::Actor* target, void* cause, void* a4) {
    if (obj == SDK::ClientInstance::get()->getLocalPlayer()) {
        AttackEvent ev { target };
        Eventing::get().dispatch(ev);
    }

    return ActorAttackHook->oFunc<decltype(&hkActorAttack)>()(obj, ret, target, cause, a4);
}

bool PlayerHooks::hkGameModeBuildBlock(SDK::GameMode* gameMode, BlockPos const* blockPos, uint8_t face, bool a4) {
    if (blockPos) {
        BuildBlockEvent ev { *blockPos, face };
        if (Eventing::get().dispatch(ev)) {
            return false;
        }
    }

    return GameModeBuildBlockHook->oFunc<decltype(&hkGameModeBuildBlock)>()(gameMode, blockPos, face, a4);
}

PlayerHooks::PlayerHooks() {
    ActorAttackHook = this->addHook(Signatures::Actor_attack.result, &hkActorAttack, "Actor::attack");
    GameModeBuildBlockHook = this->addHook(Signatures::GameMode_buildBlock.result, &hkGameModeBuildBlock,
                                           "GameMode::buildBlock");
}
