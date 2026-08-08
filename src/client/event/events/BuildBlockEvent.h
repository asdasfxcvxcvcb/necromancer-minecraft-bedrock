#pragma once
#include <util/LMath.h>
#include <util/Crypto.h>
#include <client/event/Event.h>

class BuildBlockEvent : public Cancellable {
private:
    BlockPos blockPos;
    uint8_t face;

public:
    static const uint32_t hash = TOHASH(BuildBlockEvent);

    BuildBlockEvent(BlockPos const& blockPos, uint8_t face)
        : blockPos(blockPos)
        , face(face) {}

    [[nodiscard]] BlockPos getBlockPos() const { return blockPos; }
    [[nodiscard]] uint8_t getFace() const { return face; }
};
