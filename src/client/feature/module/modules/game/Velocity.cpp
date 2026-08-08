#include "pch.h"
#include "Velocity.h"
#include "client/event/events/PacketReceiveEvent.h"
#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/player/LocalPlayer.h"
#include "mc/common/network/packet/SetActorMotionPacket.h"

Velocity::Velocity()
    : Module("Velocity", LocalizeString::get("client.module.velocity.name"),
             LocalizeString::get("client.module.velocity.desc"), GAME, nokeybind) {
    addSliderSetting("intensity", LocalizeString::get("client.module.velocity.intensity.name"),
                     LocalizeString::get("client.module.velocity.intensity.desc"), intensity, FloatValue(0.f),
                     FloatValue(100.f), FloatValue(1.f));

    listen<PacketReceiveEvent>((EventListenerFunc)&Velocity::onPacketReceive);
}

void Velocity::onPacketReceive(Event& evG) {
    auto& ev = reinterpret_cast<PacketReceiveEvent&>(evG);
    auto* packet = ev.getPacket();
    if (!packet || packet->getID() != SDK::PacketID::SET_ENTITY_MOTION) return;

    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;
    if (!lp) return;

    auto* motionPacket = static_cast<SDK::SetActorMotionPacket*>(packet);
    if (motionPacket->getRuntimeID() != lp->getRuntimeID()) return;

    float scale = 1.f - std::clamp(std::get<FloatValue>(intensity).value, 0.f, 100.f) / 100.f;
    if (scale <= 0.f) {
        ev.setCancelled(true);
        return;
    }

    Vec3 motion = motionPacket->getMotion();
    motionPacket->setMotion(Vec3 { motion.x * scale, motion.y * scale, motion.z * scale });
}
