#include "pch.h"
#include "SetActorMotionPacket.h"

namespace {
    constexpr uintptr_t runtimeIdOffset = 0x30;
    constexpr uintptr_t motionOffset = 0x38;
}

uint64_t SDK::SetActorMotionPacket::getRuntimeID() const {
    return *reinterpret_cast<const uint64_t*>(reinterpret_cast<uintptr_t>(this) + runtimeIdOffset);
}

Vec3 SDK::SetActorMotionPacket::getMotion() const {
    return *reinterpret_cast<const Vec3*>(reinterpret_cast<uintptr_t>(this) + motionOffset);
}

void SDK::SetActorMotionPacket::setMotion(Vec3 const& motion) {
    *reinterpret_cast<Vec3*>(reinterpret_cast<uintptr_t>(this) + motionOffset) = motion;
}
