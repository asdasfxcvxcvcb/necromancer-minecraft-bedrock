#include "pch.h"
#include "PacketHooks.h"
#include <mc/common/network/MinecraftPackets.h>
#include <mc/common/network/packet/AddPlayerPacket.h>
#include <mc/common/network/packet/SetActorDataPacket.h>
#include <limits>
#include <type_traits>
#include <unordered_map>

namespace {
    std::shared_ptr<Hook> SetTitlePacketRead;
    std::shared_ptr<Hook> TextPacketRead;
    std::shared_ptr<Hook> SendToServerHook;
    std::shared_ptr<Hook> CreatePacketHook;

    constexpr size_t PacketHookArraySize =
        static_cast<size_t>(std::numeric_limits<std::underlying_type_t<SDK::PacketID>>::max()) + 1;

    std::array<std::shared_ptr<Hook>, PacketHookArraySize> PacketHookArray;
    std::unordered_map<uintptr_t, std::shared_ptr<Hook>> PacketHooksByVtableSlot;
}

void PacketHooks::PacketSender_sendToServer(SDK::PacketSender* sender, SDK::Packet* packet) {
    SendPacketEvent ev { packet };

    if (Eventing::get().dispatch(ev)) {
        return;
    }

    SendToServerHook->oFunc<decltype(&PacketSender_sendToServer)>()(sender, packet);
}

std::shared_ptr<SDK::Packet> PacketHooks::MinecraftPackets_createPacket(SDK::PacketID packetId) {
    auto genPacket = CreatePacketHook->oFunc<decltype(&MinecraftPackets_createPacket)>()(packetId);

    return genPacket;
}

void PacketHooks::PacketHandlerDispatcherInstance_handle(void* instance, void* networkIdentifier,
                                                         void* netEventCallback, std::shared_ptr<SDK::Packet>& packet) {
    if (!packet) return;

    auto packetId = packet->getID();
    auto hook = PacketHookArray[static_cast<size_t>(static_cast<std::underlying_type_t<SDK::PacketID>>(packetId))];
    if (!hook && instance) {
        auto** vft = *reinterpret_cast<void***>(instance);
        auto hookIt = PacketHooksByVtableSlot.find(reinterpret_cast<uintptr_t>(vft + 1));
        if (hookIt != PacketHooksByVtableSlot.end()) {
            hook = hookIt->second;
        }
    }
    if (!hook) return;

    const bool isMainThread = Necromancer::isMainThread();
    std::shared_ptr<SDK::Packet> postVanillaPacket = isMainThread ? packet : nullptr;

    if (isMainThread) {
        PacketReceiveEvent ev { packet.get() };
        if (Eventing::get().dispatch(ev)) {
            return;
        }

        if (packetId == SDK::PacketID::TEXT) {
            auto pkt = std::static_pointer_cast<SDK::TextPacket>(packet).get();

            ClientTextEvent textEvent { pkt };
            if (Eventing::get().dispatch(textEvent)) {
                return;
            }
        } else if (packetId == SDK::PacketID::CHANGE_DIMENSION) {
            Necromancer::get().getNameTagCache().clearNetworkNameTags();
        } else if (packetId == SDK::PacketID::ADD_PLAYER) {
            SDK::AddPlayerPacket* addPlayer = static_cast<SDK::AddPlayerPacket*>(packet.get());
            uint64_t runtimeId = 0;
            std::string nameTag;
            if (addPlayer->tryGetNameTag(&runtimeId, &nameTag)) {
                Necromancer::get().getNameTagCache().recordNetworkNameTag(runtimeId, nameTag);
            }
        } else if (packetId == SDK::PacketID::SET_ENTITY_DATA) {
            SDK::SetActorDataPacket* setActorData = static_cast<SDK::SetActorDataPacket*>(packet.get());
            uint64_t runtimeId = 0;
            std::string nameTag;
            if (setActorData->tryGetNameTag(&runtimeId, &nameTag)) {
                Necromancer::get().getNameTagCache().recordNetworkNameTag(runtimeId, nameTag);
            }
        }
    }

    hook->oFunc<decltype(&PacketHandlerDispatcherInstance_handle)>()(instance, networkIdentifier, netEventCallback,
                                                                     packet);
}

PacketHooks::PacketHooks() {
    // CreatePacketHook = addHook(Signatures::MinecraftPackets_createPacket.result,
    //     MinecraftPackets_createPacket,
    //     "MinecraftPackets::createPacket");

    for (size_t i = 1; i < PacketHookArray.size(); i++) {
        auto pkt = SDK::MinecraftPackets::createPacket(
            static_cast<SDK::PacketID>(static_cast<std::underlying_type_t<SDK::PacketID>>(i)));
        if (pkt) {
            auto vft = *pkt->handler;
            auto const vtableSlot = reinterpret_cast<uintptr_t>(vft + 1);
            auto [hookIt, inserted] = PacketHooksByVtableSlot.try_emplace(vtableSlot);
            if (inserted) {
                hookIt->second = addTableSwapHook(vtableSlot, &PacketHandlerDispatcherInstance_handle, "Packet Hook");
            }
            PacketHookArray[i] = hookIt->second;
        }
    }
}

void PacketHooks::initPacketSender(SDK::PacketSender* sender) {
    uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(sender);
    SendToServerHook =
        addTableSwapHook((uintptr_t)(vtable + 2), PacketSender_sendToServer, "PacketSender::sendToServer");
}
