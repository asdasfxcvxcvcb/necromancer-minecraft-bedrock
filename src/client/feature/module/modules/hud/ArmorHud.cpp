#include "pch.h"
#include "ArmorHud.h"
#include "client/Necromancer.h"
#include "client/render/Renderer.h"
#include "client/render/asset/ItemIconCache.h"
#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/player/LocalPlayer.h"
#include "mc/common/world/Item.h"
#include "mc/common/world/ItemStack.h"
#include "mc/common/world/actor/player/Player.h"
#include "util/DrawContext.h"

namespace {
    constexpr int enchIdProtection = 0;
    constexpr wchar_t slotLabels[5] = { L'H', L'C', L'L', L'B', L'W' };

    d2d::Color durabColor(float ratio) {
        ratio = std::clamp(ratio, 0.f, 1.f);
        if (ratio > 0.5f) {
            float t = (1.f - ratio) * 2.f;
            return { 0.25f + t * 0.6f, 0.85f, 0.2f, 1.f };
        }
        return { 0.95f, 0.2f + ratio * 1.3f, 0.15f, 1.f };
    }

    // Height reserved under each slot for the durability text, so the icon, the bar
    // and the number never share pixels in either orientation.
    float textRowHeight(float size) {
        return std::max(8.f, size * 0.26f);
    }
}

ArmorHud::ArmorHud()
    : HUDModule("ArmorHud", LocalizeString::get("client.hudmodule.armorHud.name"),
                LocalizeString::get("client.hudmodule.armorHud.desc"), HUD) {
    mode.addEntry(EnumEntry(mode_vertical, LocalizeString::get("client.hudmodule.armorHud.modeVertical.name"),
                            LocalizeString::get("client.hudmodule.armorHud.mode.desc")));
    mode.addEntry(EnumEntry(mode_horizontal, LocalizeString::get("client.hudmodule.armorHud.modeHorizontal.name"),
                            LocalizeString::get("client.hudmodule.armorHud.mode.desc")));
    addEnumSetting("mode", LocalizeString::get("client.hudmodule.armorHud.mode.name"),
                   LocalizeString::get("client.hudmodule.armorHud.mode.desc"), mode);

    addSetting("showHeldItem", LocalizeString::get("client.hudmodule.armorHud.heldItem.name"),
               LocalizeString::get("client.hudmodule.armorHud.heldItem.desc"), showHeldItem);
    addSetting("showDurabilityBar", LocalizeString::get("client.hudmodule.armorHud.durabilityBar.name"),
               LocalizeString::get("client.hudmodule.armorHud.durabilityBar.desc"), showDurabilityBar);
    addSetting("showDurabilityNumber", LocalizeString::get("client.hudmodule.armorHud.durabilityNumber.name"),
               LocalizeString::get("client.hudmodule.armorHud.durabilityNumber.desc"), showDurabilityNumber);
    addSetting("showProtection", LocalizeString::get("client.hudmodule.armorHud.protection.name"),
               LocalizeString::get("client.hudmodule.armorHud.protection.desc"), showProtection);

    addSetting("textColor", LocalizeString::get("client.hudmodule.armorHud.textColor.name"),
               LocalizeString::get("client.hudmodule.armorHud.textColor.desc"), textColor);
    addSetting("slotColor", LocalizeString::get("client.hudmodule.armorHud.slotColor.name"),
               LocalizeString::get("client.hudmodule.armorHud.slotColor.desc"), slotColor);
    addSetting("barBgColor", LocalizeString::get("client.hudmodule.armorHud.barBgColor.name"),
               LocalizeString::get("client.hudmodule.armorHud.barBgColor.desc"), barBgColor,
               "showDurabilityBar"_istrue);
}

int ArmorHud::collectPieces(SDK::Player* player, Piece out[slot_count]) {
    for (int i = 0; i < slot_count; i++) out[i] = Piece {};

    int found = 0;

    auto pushPiece = [&](int idx, SDK::ItemStack* stack) {
        if (!stack || !stack->valid || stack->itemCount == 0) return;
        SDK::Item* raw = stack->getItem();
        if (!raw) return;

        Piece& piece = out[idx];
        piece.stack = stack;
        piece.valid = true;
        piece.label = slotLabels[idx];
        piece.itemId = raw->id.getString();

        int maxDmg = raw->getMaxDamage();
        if (maxDmg > 0) {
            int cur = static_cast<int>(stack->getDamageValue());
            int remaining = std::max(0, maxDmg - cur);
            piece.hasDurability = true;
            piece.current = remaining;
            piece.max = maxDmg;
            piece.ratio = static_cast<float>(remaining) / static_cast<float>(maxDmg);
        }
        piece.protection = stack->getEnchantValue(enchIdProtection);
        found++;
    };

    for (int slot = 0; slot < 4; slot++) {
        pushPiece(slot, player->getArmor(slot));
    }

    if (std::get<BoolValue>(showHeldItem) && player->supplies && player->supplies->inventory) {
        pushPiece(4, player->supplies->inventory->getItem(player->supplies->selectedSlot));
    }

    return found;
}

void ArmorHud::drawIcon(DrawUtil& dc, Piece const& piece, Vec2 pos, float size) {
    if (ItemIconCache::drawItem(dc, piece.stack, pos, size)) return;

    // No embedded texture, which happens on servers shipping custom items.
    d2d::Rect slotRc = { pos.x, pos.y, pos.x + size, pos.y + size };
    dc.fillRoundedRectangle(slotRc, std::get<ColorValue>(slotColor).getMainColor(), size * 0.12f);
    std::wstring tag(1, piece.label);
    dc.drawText(slotRc, tag, d2d::Color(std::get<ColorValue>(textColor).getMainColor()),
                Renderer::FontSelection::PrimaryRegular, std::max(9.f, size * 0.4f), DWRITE_TEXT_ALIGNMENT_CENTER,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void ArmorHud::drawOutlinedText(DrawUtil& dc, d2d::Rect const& rc, std::wstring const& text, d2d::Color const& col,
                                float fontSize, DWRITE_TEXT_ALIGNMENT align) {
    constexpr float off = 1.f;
    d2d::Color outline(0.f, 0.f, 0.f, col.a);

    static constexpr float dirs[8][2] = { { -off, 0.f }, { off, 0.f },   { 0.f, -off },  { 0.f, off },
                                          { -off, -off }, { off, -off }, { -off, off }, { off, off } };

    for (auto const& d : dirs) {
        d2d::Rect shifted = { rc.left + d[0], rc.top + d[1], rc.right + d[0], rc.bottom + d[1] };
        dc.drawText(shifted, text, outline, Renderer::FontSelection::PrimaryRegular, fontSize, align,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    dc.drawText(rc, text, col, Renderer::FontSelection::PrimaryRegular, fontSize, align,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void ArmorHud::drawPiece(DrawUtil& dc, Piece const& piece, Vec2 pos, float size, float cellW) {
    drawIcon(dc, piece, pos, size);

    d2d::Color txtCol(std::get<ColorValue>(textColor).getMainColor());
    float barH = std::max(2.f, size * 0.1f);

    if (std::get<BoolValue>(showDurabilityBar) && piece.hasDurability) {
        d2d::Rect barBg = { pos.x, pos.y + size - barH, pos.x + size, pos.y + size };
        dc.fillRectangle(barBg, std::get<ColorValue>(barBgColor).getMainColor());

        d2d::Rect barFill = barBg;
        barFill.right = barFill.left + barFill.getWidth() * piece.ratio;
        dc.fillRectangle(barFill, durabColor(piece.ratio));
    }

    if (std::get<BoolValue>(showProtection) && piece.protection > 0) {
        std::wstring p = L"P" + std::to_wstring(piece.protection);
        float pSize = std::max(8.f, size * 0.26f);
        d2d::Rect pRc = { pos.x, pos.y + 1.f, pos.x + size - 2.f, pos.y + 1.f + pSize };
        drawOutlinedText(dc, pRc, p, txtCol, pSize, DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    // Sits in its own row under the slot, centred on the cell rather than the icon so
    // neighbouring numbers cannot run into each other in horizontal mode.
    if (std::get<BoolValue>(showDurabilityNumber) && piece.hasDurability) {
        float rowH = textRowHeight(size);
        std::wstring txt = std::to_wstring(piece.current) + L"/" + std::to_wstring(piece.max);

        float fontSize = rowH * 0.85f;
        float avail = std::max(1.f, cellW - 2.f);
        Vec2 ts = dc.getTextSize(txt, Renderer::FontSelection::PrimaryRegular, fontSize);
        if (ts.x > avail && ts.x > 0.f) {
            fontSize = std::max(6.f, fontSize * (avail / ts.x));
        }

        float cx = pos.x + size * 0.5f;
        d2d::Rect numRc = { cx - cellW * 0.5f, pos.y + size, cx + cellW * 0.5f, pos.y + size + rowH };
        drawOutlinedText(dc, numRc, txt, txtCol, fontSize, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void ArmorHud::render(DrawUtil& dc, bool isDefault, bool inEditor) {
    if (isDefault) return;

    constexpr float size = 32.f;
    bool vertical = mode.getSelectedKey() == mode_vertical;
    float gap = std::max(2.f, size * 0.12f);
    bool showNumbers = std::get<BoolValue>(showDurabilityNumber);
    float rowH = showNumbers ? textRowHeight(size) : 0.f;
    float cellH = size + rowH;

    // Icons pack tightly. When durability numbers are shown horizontally the text is
    // wider than its icon, so nudge the step out just enough to keep neighbouring
    // numbers from touching, rather than a fixed oversized cell.
    float cellW = size;
    float stepX = size + gap;
    if (showNumbers && !vertical) {
        cellW = size * 1.45f;
        stepX = cellW + gap;
    } else if (showNumbers) {
        cellW = size * 1.6f;
    }

    auto setBounds = [&](int count) {
        if (count <= 0) {
            this->rect.right = rect.left;
            this->rect.bottom = rect.top;
            return;
        }
        if (vertical) {
            this->rect.right = rect.left + size;
            this->rect.bottom = rect.top + static_cast<float>(count) * (cellH + gap) - gap;
        } else {
            this->rect.right = rect.left + static_cast<float>(count) * stepX - gap;
            this->rect.bottom = rect.top + cellH;
        }
    };

    if (dc.isMinecraft()) {
        auto& mcDc = static_cast<MCDrawUtil&>(dc);
        if (!mcDc.renderCtx || !mcDc.renderCtx->screenContext || !mcDc.renderCtx->cinst ||
            !mcDc.renderCtx->cinst->minecraftGame || !mcDc.scn || !mcDc.scn->matrix || !mcDc.font) {
            return;
        }
    }

    auto ci = SDK::ClientInstance::get();
    auto lp = ci ? ci->getLocalPlayer() : nullptr;

    Piece pieces[slot_count];
    int found = lp ? collectPieces(lp, pieces) : 0;

    if (found == 0) {
        // Nothing equipped. Only the editor gets placeholder slots to grab; during
        // normal play the module takes up no screen space at all.
        if (!inEditor) {
            setBounds(0);
            return;
        }

        setBounds(4);
        Vec2 ghost = { 0.f, 0.f };
        for (int i = 0; i < 4; i++) {
            d2d::Rect slotRc = { ghost.x, ghost.y, ghost.x + size, ghost.y + size };
            dc.fillRoundedRectangle(slotRc, std::get<ColorValue>(slotColor).getMainColor(), size * 0.12f);
            std::wstring tag(1, slotLabels[i]);
            dc.drawText(slotRc, tag, d2d::Color(std::get<ColorValue>(textColor).getMainColor()),
                        Renderer::FontSelection::PrimaryRegular, std::max(9.f, size * 0.4f),
                        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (vertical) ghost.y += cellH + gap;
            else ghost.x += stepX;
        }
        return;
    }

    Vec2 pos = { 0.f, 0.f };
    int drawn = 0;

    for (int i = 0; i < slot_count; i++) {
        if (!pieces[i].valid) continue;
        drawPiece(dc, pieces[i], pos, size, cellW);
        if (vertical) pos.y += cellH + gap;
        else pos.x += stepX;
        drawn++;
    }

    setBounds(drawn);
}
