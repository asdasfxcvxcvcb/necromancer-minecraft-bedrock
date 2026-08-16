#include "pch.h"
#include "Level.h"
#include "mc/Addresses.h"

void SDK::Level::playSoundEvent(std::string const& text, Vec3 const& pos, float vol, float pitch) {
    float unk[4] {};
    memory::callVirtual<void>(this, Signatures::VtableIndex::Level::playSoundEvent, text, pos, vol, pitch, unk);
}

void SDK::Level::getRuntimeActorList(std::vector<SDK::Actor*>& out) {
    out.clear();
    memory::callVirtual<void, std::vector<Actor*>&>(this, Signatures::VtableIndex::Level::getRuntimeActorList, out);
}

std::vector<SDK::Actor*> SDK::Level::getRuntimeActorList() {
    std::vector<Actor*> list;
    getRuntimeActorList(list);
    return list;
}

std::unordered_map<UUID, SDK::PlayerListEntry>* SDK::Level::getPlayerList() {
    return *reinterpret_cast<std::unordered_map<UUID, SDK::PlayerListEntry>**>(reinterpret_cast<uintptr_t>(this) +
                                                                               0x4E0);
}

SDK::HitResult* SDK::Level::getHitResult() {
    return memory::callVirtual<HitResult*>(this, Signatures::VtableIndex::Level::getHitResult);
}

SDK::HitResult* SDK::Level::getLiquidHitResult() {
    return reinterpret_cast<SDK::HitResult*>(
        memory::callVirtual<uintptr_t>(this, Signatures::VtableIndex::Level::getLiquidHitResult));
}

bool SDK::Level::isClientSide() {
    return memory::callVirtual<bool>(this, Signatures::VtableIndex::Level::isClientSide);
}

const std::string& SDK::Level::getLevelName() {
    return levelData->levelName;
}
