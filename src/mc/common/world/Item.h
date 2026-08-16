#pragma once
#include <string>
#include "mc/deps/core/StringUtils.h"
#include "mc/Util.h"
#include "util/memory.h"
#include <mc/Addresses.h>

namespace SDK {
    class Item {
    public:
        CLASS_FIELD(std::string, atlas, 0x10);          // atlas.items
        CLASS_FIELD(std::string, translateName, 0xB0);  // item.bow
        CLASS_FIELD(HashedString, id, 0xD0);            // bow
        CLASS_FIELD(std::string, _namespace, 0x100);    // minecraft
        CLASS_FIELD(HashedString, namespacedId, 0x120); // minecraft:bow

        int getMaxUseDuration(class ItemStackBase* item) {
            return memory::callVirtual<int>(this, Signatures::VtableIndex::Item::getMaxUseDuration, item);
        }

        bool isGlint(class ItemStackBase* item) {
            return memory::callVirtual<bool>(this, Signatures::VtableIndex::Item::isGlint, item);
        }

        int getMaxDamage() {
            return memory::callVirtual<int>(this, Signatures::VtableIndex::Item::getMaxDamage);
        }

        bool canDestroySpecial(class Block const* block) {
            return memory::callVirtual<bool>(this, Signatures::VtableIndex::Item::canDestroySpecial, block);
        }

        float getDestroySpeed(class ItemStackBase const* item, class Block const* block) {
            return memory::callVirtual<float>(this, Signatures::VtableIndex::Item::getDestroySpeed, item, block);
        }

        short getDamageValue(class CompoundTag* tag) {
            // TODO: fix this
            return reinterpret_cast<short (*)(Item*, CompoundTag*)>(Signatures::ItemStackBase_getDamageValue.result)(
                this, tag);
        }

        virtual ~Item() = 0;
    };
}
