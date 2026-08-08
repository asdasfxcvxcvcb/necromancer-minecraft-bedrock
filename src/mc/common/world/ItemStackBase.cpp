#include "pch.h"
#include "ItemStackBase.h"

std::string SDK::ItemStackBase::getHoverName() {
    if (!Signatures::ItemStackBase_getHoverName.result) return {};

    std::string out;
    reinterpret_cast<std::string*(__fastcall*)(ItemStackBase*, std::string*)>(
        Signatures::ItemStackBase_getHoverName.result)(this, &out);
    return out;
}

std::map<int, int> SDK::ItemStackBase::gatherEnchants() {
    std::map<int, int> out;
    if (!tag) return out;

    auto enchTag = tag->get("ench");
    if (!enchTag || enchTag->getId() != static_cast<uint8_t>(TagType::List)) return out;

    auto list = reinterpret_cast<ListTag*>(enchTag);
    for (auto entry : list->val) {
        if (!entry || entry->getId() != static_cast<uint8_t>(TagType::Compound)) continue;

        auto comp = reinterpret_cast<CompoundTag*>(entry);
        int id = -1;
        int lvl = 0;

        if (auto idTag = comp->get("id")) {
            if (idTag->getId() == static_cast<uint8_t>(TagType::Byte)) {
                id = reinterpret_cast<ByteTag*>(idTag)->val;
            } else if (idTag->getId() == static_cast<uint8_t>(TagType::Short)) {
                id = reinterpret_cast<ShortTag*>(idTag)->val;
            }
        }
        if (auto lvlTag = comp->get("lvl")) {
            if (lvlTag->getId() == static_cast<uint8_t>(TagType::Byte)) {
                lvl = reinterpret_cast<ByteTag*>(lvlTag)->val;
            } else if (lvlTag->getId() == static_cast<uint8_t>(TagType::Short)) {
                lvl = reinterpret_cast<ShortTag*>(lvlTag)->val;
            }
        }

        if (id >= 0) out[id] = lvl;
    }

    return out;
}

int SDK::ItemStackBase::getEnchantValue(int id) {
    auto enchants = gatherEnchants();
    auto it = enchants.find(id);
    return it != enchants.end() ? it->second : 0;
}
