#pragma once
#include "client/feature/module/Module.h"

class ItemSwitcher : public Module {
public:
    ItemSwitcher();

    void onEnable() override;
    void onTick(Event& evG);

    void setTargetItem(std::string const& id);

    ValueType targetItem = TextValue(L"");

private:
    ValueType openPicker = ButtonValue();
    ValueType switchOnce = BoolValue(true);

    bool hasSwitched = false;
};