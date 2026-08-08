#pragma once
#include "client/event/Event.h"
#include "util/Crypto.h"

class MouseWheelEvent : public Cancellable {
public:
    static const uint32_t hash = TOHASH(MouseWheelEvent);

    MouseWheelEvent(int delta)
        : delta(delta) {}

    [[nodiscard]] int getDelta() const { return delta; }

private:
    int delta;
};
