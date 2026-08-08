#pragma once
#include "../../Module.h"

class DisableMouseWheel : public Module {
public:
    DisableMouseWheel();

    void onMouseWheel(Event& evG);
    void onClick(Event& evG);

private:
    bool shouldBlock() const;
};
