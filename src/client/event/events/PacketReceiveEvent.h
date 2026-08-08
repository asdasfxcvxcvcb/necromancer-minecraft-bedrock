#pragma once
#include "client/event/Event.h"
#include "util/Crypto.h"
#include <mc/common/network/Packet.h>

class PacketReceiveEvent : public Cancellable {
public:
    static const uint32_t hash = TOHASH(PacketReceiveEvent);

    PacketReceiveEvent(SDK::Packet* pkt)
        : packet(pkt) {}

    [[nodiscard]] SDK::Packet* getPacket() { return packet; }

private:
    SDK::Packet* packet;
};
