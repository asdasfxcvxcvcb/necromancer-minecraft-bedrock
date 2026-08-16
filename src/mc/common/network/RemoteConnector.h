#pragma once
#include "GameConnectionInfo.h"
#include "mc/Addresses.h"
#include "util/memory.h"

namespace SDK {
    class RemoteConnector {
    public:
        Social::GameConnectionInfo* getConnectedGameInfo() {
            return memory::callVirtual<Social::GameConnectionInfo*>(
                this, Signatures::VtableIndex::RemoteConnector::getConnectedGameInfo);
        }
    };
}
