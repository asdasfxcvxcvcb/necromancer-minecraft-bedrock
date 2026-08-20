#pragma once
#include "../Packet.h"

namespace SDK {
    class InteractPacket : public Packet {
    public:
        static constexpr uint8_t ActionOpenInventory = 4;

        uint8_t action;

    private:
        uint8_t pad0[7];

    public:
        uint64_t entityId;
        float pos[3];

    private:
        uint32_t pad1;

    public:
        uint32_t tail;
    };

    static_assert(sizeof(InteractPacket) == 0x58);
}
