#pragma once
#include "../Packet.h"
#include "util/LMath.h"

namespace SDK {
    class SetActorMotionPacket : public Packet {
    public:
        uint64_t getRuntimeID() const;
        Vec3 getMotion() const;
        void setMotion(Vec3 const& motion);
    };
}
