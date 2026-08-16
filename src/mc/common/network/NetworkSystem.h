#pragma once
#include "mc/Util.h"
#include "RemoteConnectorComposite.h"

namespace SDK {
    class NetworkSystem {
    public:
        CLASS_FIELD(RemoteConnectorComposite*, remoteConnector, Signatures::FieldOffset::NetworkSystem::remoteConnector);
    };
}
