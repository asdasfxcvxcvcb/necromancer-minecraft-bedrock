#pragma once
#include "client/feature/module/Module.h"

// Hidden from every ClickGUI category tab on purpose (it is listed in no tab in
// ClickGUI::isModuleInTab), so it can only be reached through the search box.
// Toggling it drives ModuleProfiler, which appends a per-module timing report to
// Logs\fps_tester.txt every 10 seconds.
class DebugInfo : public Module {
public:
    DebugInfo();

    void onEnable() override;
    void onDisable() override;
};
