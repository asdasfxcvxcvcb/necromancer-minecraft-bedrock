#include "pch.h"
#include "CommandRequestPacket.h"
#include "mc/Util.h"

void SDK::CommandRequestPacket::applyCommand(std::string const& commandText) {
    this->origin.type = CommandOriginType::Player;
    this->command = commandText;
    this->InternalSource = true;
}
