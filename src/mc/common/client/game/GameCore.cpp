#include "pch.h"
#include "GameCore.h"

#include "Platform_GameCore.h"

SDK::GameCore* SDK::GameCore::get() {
    const auto platform = SDK::Platform_GameCore::get();
    return platform ? platform->getGameCore() : nullptr;
}
