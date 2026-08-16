#pragma once
#include "mc/Addresses.h"
#include "mc/Util.h"
#include "NetherNetConnector.h"
#include "RakNetConnector.h"

namespace SDK {
    class RemoteConnectorComposite {
    public:
        static RemoteConnectorComposite* get();
        static Social::GameConnectionInfo* getConnectionInfo();
        RemoteConnector* getActiveConnector();

        CLASS_FIELD(NetherNetConnector*, netherNetConnector,
                    Signatures::FieldOffset::RemoteConnectorComposite::netherNetConnector);
        CLASS_FIELD(RakNetConnector*, rakNetConnector,
                    Signatures::FieldOffset::RemoteConnectorComposite::rakNetConnector);
    };
}
