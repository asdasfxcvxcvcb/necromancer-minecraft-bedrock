#pragma once
#include "../Packet.h"

namespace SDK {
    class ContainerOpenPacket : public Packet {
    public:
        static constexpr int8_t TypeInventory = -1;

        uint8_t containerId;
        int8_t containerType;

    private:
        uint8_t pad0[2];

    public:
        int32_t x;
        int32_t y;
        int32_t z;
        int64_t entityUniqueId;
        uint32_t flag;
    };

    static_assert(sizeof(ContainerOpenPacket) == 0x50);
}
