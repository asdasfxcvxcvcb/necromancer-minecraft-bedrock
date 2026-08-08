#include "pch.h"
#include "AntiObs.h"
#include "client/Necromancer.h"
#include "client/feature/module/ModuleManager.h"

AntiObs::AntiObs()
    : Module("AntiObs", LocalizeString::get("client.module.antiObs.name"),
             LocalizeString::get("client.module.antiObs.desc"), GAME) {
}

bool AntiObs::isActive() {
    static AntiObs* cached = nullptr;
    static bool resolved = false;
    if (!resolved) {
        auto mod = Necromancer::getModuleManager().find("AntiObs");
        cached = mod ? static_cast<AntiObs*>(mod.get()) : nullptr;
        resolved = true;
    }
    return cached && cached->isEnabled();
}
