#pragma once
#include "client/feature/module/Module.h"

namespace SDK {
    class Block;
    class Inventory;
}

class AutoBlock : public Module {
public:
    AutoBlock();

    void onTick(Event& evG);
    void onDisable() override;

private:
    ValueType threshold = FloatValue(0.f);
    ValueType sameBlockOnly = BoolValue(false);
    ValueType ignoreTnt = BoolValue(true);
    ValueType scaffoldAware = BoolValue(true);

    SDK::Block* lastBlock = nullptr;
    int lastSlot = -1;

    int scaffoldHeadroom();    int findRefillSlot(SDK::Inventory* inv, int currentSlot, SDK::Block* wanted, int minCount, bool sameOnly,
                       bool skipTnt) const;
};
