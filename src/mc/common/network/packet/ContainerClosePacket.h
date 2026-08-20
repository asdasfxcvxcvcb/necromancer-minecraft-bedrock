#pragma once
#include "../Packet.h"

namespace SDK {
    class ContainerClosePacket : public Packet {
    public:
        uint8_t containerId;
        int8_t containerType;
        bool serverInitiated;

    private:
        uint8_t pad0;

    public:
        uint32_t flag;
    };

    static_assert(sizeof(ContainerClosePacket) == 0x38);
}
