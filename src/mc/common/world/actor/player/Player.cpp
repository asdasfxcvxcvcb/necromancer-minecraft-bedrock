#include "pch.h"
#include "Player.h"
#include "util/Util.h"
#include "util/memory.h"
#include "mc/Addresses.h"

void SDK::Player::displayClientMessage(std::string const& message) {
    std::optional<std::string> opt = {};
    memory::callVirtual<void>(this, Signatures::VtableIndex::Player::displayClientMessage, message, opt);
    return;
}

SDK::MoveInputComponent* SDK::Player::getMoveInputComponent() {
    return this->tryGetComponent<MoveInputComponent>();
}

std::string SDK::Player::getXUID() {
    return memory::callVirtual<std::string>(this, Signatures::VtableIndex::Player::getXUID);
}
