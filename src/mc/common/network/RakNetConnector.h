#pragma once
#include "RemoteConnector.h"

namespace SDK {
    class RakNetConnector : public RemoteConnector {
    public:
        static RakNetConnector* get();

        RakNetConnector() = delete;
    };
}
