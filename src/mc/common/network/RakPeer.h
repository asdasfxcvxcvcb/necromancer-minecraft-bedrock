#pragma once
#include "util/memory.h"
#include <mc/Addresses.h>

namespace SDK {
    class RakPeer {
    public:
        bool Ping(const char* addy, unsigned short port, bool b1, unsigned int u1) {
            return memory::callVirtual<bool>(this, Signatures::VtableIndex::RakPeer::ping, addy, port, b1, u1);
        }

        int GetAveragePing(std::string const& addressOrGuid) {
            return memory::callVirtual<int>(this, Signatures::VtableIndex::RakPeer::getAveragePing, addressOrGuid);
        }
    };
}
