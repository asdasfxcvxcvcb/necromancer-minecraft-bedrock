#pragma once
#include "Actor.h"
#include "util/memory.h"
#include <mc/Addresses.h>

namespace SDK {
    class Mob : public Actor {
    public:
        void setSprinting(bool b) {
            memory::callVirtual<void>(this, Signatures::VtableIndex::Mob::setSprinting, b);
        }

        int getItemUseDuration() {
            return memory::callVirtual<int>(this, Signatures::VtableIndex::Mob::getItemUseDuration);
        }
    };
}
