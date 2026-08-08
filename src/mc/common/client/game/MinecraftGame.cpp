#include "pch.h"
#include "MinecraftGame.h"
#include "mc/Addresses.h"

bool SDK::MinecraftGame::isCursorGrabbed() {
    return hat::member_at<bool>(this, 0x1D8);
}

// Directly writes the "cursor grabbed" flag without going through the game's
// grab/release routines (which touch gameplay-session state). Used to briefly
// spoof the grabbed state so native movement runs while the menu is open.
void SDK::MinecraftGame::setCursorGrabbedRaw(bool grabbed) {
    hat::member_at<bool>(this, 0x1D8) = grabbed;
}

SDK::ClientInstance* SDK::MinecraftGame::getPrimaryClientInstance() {
    const auto map = hat::member_at<std::map<uint8_t, std::shared_ptr<ClientInstance>>>(this, 0x908);
    return map.at(0).get();
}
