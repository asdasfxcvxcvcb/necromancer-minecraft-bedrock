#pragma once
#include "mc/Addresses.h"
#include "mc/Util.h"
#include <string>

namespace SDK::Social {
    class ThirdPartyInfo {
    public:
        CLASS_FIELD(std::string, creatorId, Signatures::FieldOffset::ThirdPartyInfo::creatorId);
        CLASS_FIELD(std::string, creatorName, Signatures::FieldOffset::ThirdPartyInfo::creatorName);
        CLASS_FIELD(std::string, storagePageId, Signatures::FieldOffset::ThirdPartyInfo::storagePageId);
        CLASS_FIELD(bool, requireXboxLive, Signatures::FieldOffset::ThirdPartyInfo::requireXboxLive);
        CLASS_FIELD(std::string, experienceId, Signatures::FieldOffset::ThirdPartyInfo::experienceId);
    };

    class GameConnectionInfo {
    public:
        CLASS_FIELD(std::string, hostIpAddress, Signatures::FieldOffset::GameConnectionInfo::hostIpAddress);
        CLASS_FIELD(std::string, unresolvedUrl, Signatures::FieldOffset::GameConnectionInfo::unresolvedUrl);
        CLASS_FIELD(std::string, serverRegion, Signatures::FieldOffset::GameConnectionInfo::serverRegion);
        CLASS_FIELD(int, serviceQuality, Signatures::FieldOffset::GameConnectionInfo::serviceQuality);
        CLASS_FIELD(int, port, Signatures::FieldOffset::GameConnectionInfo::port);
        CLASS_FIELD(ThirdPartyInfo, thirdPartyServerInfo,
                    Signatures::FieldOffset::GameConnectionInfo::thirdPartyServerInfo);
    };
}
