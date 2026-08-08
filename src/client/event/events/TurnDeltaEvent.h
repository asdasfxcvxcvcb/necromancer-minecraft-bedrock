#pragma once
#include "client/event/Event.h"
#include "mc/common/client/player/LocalPlayer.h"
#include "util/Crypto.h"
#include "util/LMath.h"

class TurnDeltaEvent : public Event {
public:
    static const uint32_t hash = TOHASH(TurnDeltaEvent);

    TurnDeltaEvent(SDK::LocalPlayer* player, Vec2& delta)
        : player(player)
        , delta(delta) {}

    SDK::LocalPlayer* getPlayer() const { return player; }
    Vec2& getDelta() const { return delta; }

private:
    SDK::LocalPlayer* player;
    Vec2& delta;
};
