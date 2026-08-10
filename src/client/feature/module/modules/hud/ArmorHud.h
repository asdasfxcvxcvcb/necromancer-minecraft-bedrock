#pragma once
#include "../../HUDModule.h"

namespace SDK {
    class ItemStack;
    class Player;
}

class ArmorHud : public HUDModule {
public:
    ArmorHud();

    void render(DrawUtil& ctx, bool isDefault, bool inEditor) override;

private:
    EnumData mode;
    static constexpr int mode_vertical = 0;
    static constexpr int mode_horizontal = 1;

    static constexpr int slot_count = 5;

    ValueType showDurabilityBar = BoolValue(true);
    ValueType showDurabilityNumber = BoolValue(false);
    ValueType showProtection = BoolValue(true);
    ValueType showHeldItem = BoolValue(true);
    ValueType textColor = ColorValue(1.f, 1.f, 1.f, 1.f);
    ValueType barBgColor = ColorValue(0.f, 0.f, 0.f, 0.6f);
    ValueType slotColor = ColorValue(0.15f, 0.15f, 0.18f, 0.55f);

    struct Piece {
        SDK::ItemStack* stack = nullptr;
        std::string itemId;
        bool valid = false;
        bool hasDurability = false;
        float ratio = 1.f;
        int current = 0;
        int max = 0;
        int protection = 0;
        wchar_t label = L'?';
    };

    int collectPieces(SDK::Player* player, Piece out[slot_count]);
    void drawPiece(DrawUtil& dc, Piece const& piece, Vec2 pos, float size, float cellW);
    void drawIcon(DrawUtil& dc, Piece const& piece, Vec2 pos, float size);
    void drawOutlinedText(DrawUtil& dc, d2d::Rect const& rc, std::wstring const& text, d2d::Color const& col,
                          float fontSize, DWRITE_TEXT_ALIGNMENT align);
};
