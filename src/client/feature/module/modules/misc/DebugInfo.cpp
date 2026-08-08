#include "pch.h"
#include "DebugInfo.h"
#include "client/misc/ModuleProfiler.h"

DebugInfo::DebugInfo()
    : Module("debug_info", LocalizeString::get("client.module.debugInfo.name"),
             LocalizeString::get("client.module.debugInfo.desc"), GAME, nokeybind) {
}

void DebugInfo::onEnable() {
    ModuleProfiler::get().setEnabled(true);
}

void DebugInfo::onDisable() {
    ModuleProfiler::get().setEnabled(false);
}
