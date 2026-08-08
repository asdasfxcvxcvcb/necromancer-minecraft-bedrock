#include "pch.h"
#include "LegitScaffold.h"

#include "client/event/events/BuildBlockEvent.h"
#include "client/Necromancer.h"
#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/player/LocalPlayer.h"
#include "mc/common/world/actor/player/PlayerInventory.h"
#include "mc/common/world/ItemStack.h"

namespace {
    int floori(float v) {
        return static_cast<int>(std::floor(v));
    }

    Vec3 feetPos(SDK::LocalPlayer* lp) {
        if (lp->aabbShape) {
            AABB& box = lp->aabbShape->boundingBox;
            return { (box.lower.x + box.higher.x) * 0.5f, box.lower.y, (box.lower.z + box.higher.z) * 0.5f };
        }
        Vec3 pos = lp->getPos();
        return { pos.x, pos.y - 1.62f, pos.z };
    }
}

LegitScaffold::LegitScaffold()
    : Module("LegitScaffold", LocalizeString::get("client.module.legitScaffold.name"),
             LocalizeString::get("client.module.legitScaffold.desc"), GAME, nokeybind) {
    listen<BuildBlockEvent>(static_cast<EventListenerFunc>(&LegitScaffold::onBuildBlock));

    addSetting("allowUp", LocalizeString::get("client.module.legitScaffold.allowUp.name"),
               LocalizeString::get("client.module.legitScaffold.allowUp.desc"), allowUp);
}

void LegitScaffold::onBuildBlock(Event& evG) {
    auto& ev = reinterpret_cast<BuildBlockEvent&>(evG);

    auto ci = SDK::ClientInstance::get();
    auto plr = ci ? ci->getLocalPlayer() : nullptr;
    if (!plr || !plr->supplies || !plr->supplies->inventory) return;

    int sel = plr->supplies->selectedSlot;
    if (sel < 0 || sel >= 9) return;

    auto held = plr->supplies->inventory->getItem(sel);
    if (!held || held->itemCount <= 0 || !held->block) return;

    if (!shouldAllow(ev.getBlockPos(), ev.getFace())) {
        ev.setCancelled();
    }
}

bool LegitScaffold::shouldAllow(BlockPos const& blockPos, uint8_t face) const {
    auto ci = SDK::ClientInstance::get();
    auto plr = ci ? ci->getLocalPlayer() : nullptr;
    if (!plr) return true;

    if (face == FACE_DOWN) return false;
    if (face == FACE_UP) return std::get<BoolValue>(allowUp).value && isBelowFeet(blockPos);

    float nx = 0.f;
    float nz = 0.f;
    switch (face) {
    case FACE_NORTH:
        nz = -1.f;
        break;
    case FACE_SOUTH:
        nz = 1.f;
        break;
    case FACE_WEST:
        nx = -1.f;
        break;
    case FACE_EAST:
        nx = 1.f;
        break;
    default:
        return false;
    }

    Vec3 feet = feetPos(plr);
    float dx = feet.x - (static_cast<float>(blockPos.x) + 0.5f);
    float dz = feet.z - (static_cast<float>(blockPos.z) + 0.5f);

    return (nx * dx + nz * dz) >= -0.0001f;
}

bool LegitScaffold::isBelowFeet(BlockPos const& blockPos) const {
    auto ci = SDK::ClientInstance::get();
    auto plr = ci ? ci->getLocalPlayer() : nullptr;
    if (!plr) return false;

    Vec3 feet = feetPos(plr);

    if (floori(feet.x) != blockPos.x || floori(feet.z) != blockPos.z) return false;

    return blockPos.y < floori(feet.y + 0.05f);
}
