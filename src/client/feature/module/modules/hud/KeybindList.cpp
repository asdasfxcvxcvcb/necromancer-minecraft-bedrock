#include "pch.h"
#include "KeybindList.h"
#include "client/misc/KeybindManager.h"
#include <client/Necromancer.h>

KeybindList::KeybindList()
    : HUDModule("KeybindList", LocalizeString::get("client.hudmodule.keybindList.name"),
                LocalizeString::get("client.hudmodule.keybindList.desc"), HUD) {
    addSliderSetting("textSize", LocalizeString::get("client.hudmodule.keybindList.textSize.name"),
                     LocalizeString::get("client.hudmodule.keybindList.textSize.desc"), textSize, FloatValue(8.f),
                     FloatValue(40.f), FloatValue(0.5f));
    addSliderSetting("padding", LocalizeString::get("client.hudmodule.keybindList.padding.name"),
                     LocalizeString::get("client.hudmodule.keybindList.padding.desc"), padding, FloatValue(0.f),
                     FloatValue(12.f), FloatValue(0.25f));
    addSliderSetting("gap", LocalizeString::get("client.hudmodule.keybindList.gap.name"),
                     LocalizeString::get("client.hudmodule.keybindList.gap.desc"), gap, FloatValue(0.f),
                     FloatValue(10.f), FloatValue(0.25f));
    addSetting("showKey", LocalizeString::get("client.hudmodule.keybindList.showKey.name"),
               LocalizeString::get("client.hudmodule.keybindList.showKey.desc"), showKey);
    addSetting("background", LocalizeString::get("client.hudmodule.keybindList.background.name"),
               LocalizeString::get("client.hudmodule.keybindList.background.desc"), background);
    addSetting("animate", LocalizeString::get("client.hudmodule.keybindList.animate.name"),
               LocalizeString::get("client.hudmodule.keybindList.animate.desc"), animate);

    addSetting("bgCol", LocalizeString::get("client.hudmodule.keybindList.bgCol.name"),
               LocalizeString::get("client.hudmodule.keybindList.bgCol.desc"), bgCol, "background"_istrue);
    addSetting("textCol", LocalizeString::get("client.hudmodule.keybindList.textCol.name"),
               LocalizeString::get("client.hudmodule.keybindList.textCol.desc"), textCol);
    addSetting("activeCol", LocalizeString::get("client.hudmodule.keybindList.activeCol.name"),
               LocalizeString::get("client.hudmodule.keybindList.activeCol.desc"), activeCol);
}

void KeybindList::render(DrawUtil& dc, bool, bool inEditor) {
    auto& mgr = KeybindManager::get();

    float ts = std::get<FloatValue>(textSize);
    float pad = std::get<FloatValue>(padding);
    float rowGap = std::get<FloatValue>(gap);
    bool showBg = std::get<BoolValue>(background);
    bool doAnim = std::get<BoolValue>(animate);
    bool showKeySuffix = std::get<BoolValue>(showKey);
    auto font = Renderer::FontSelection::SecondaryLight;
    d2d::Color textColor(std::get<ColorValue>(textCol).getMainColor());
    d2d::Color activeColor(std::get<ColorValue>(activeCol).getMainColor());
    d2d::Color bgColor(std::get<ColorValue>(bgCol).getMainColor());

    struct Row {
        std::wstring text;
        bool active;
        float width;
        float anim;
    };

    float animDt = Necromancer::getRenderer().getDeltaTime() / 5.f;
    std::vector<Row> rows;
    for (auto& bind : mgr.getBinds()) {
        bool shown = mgr.isShown(bind);
        float& anim = anims[bind.name];
        anim = doAnim ? std::lerp(anim, shown ? 1.f : 0.f, animDt) : (shown ? 1.f : 0.f);
        if (!shown && anim < 0.01f) {
            anim = 0.f;
            continue;
        }

        std::wstring text = util::StrToWStr(bind.name);
        if (showKeySuffix) {
            if (bind.kind == KeybindManager::KindIf) {
                auto graphText = KeybindManager::describeGraph(bind);
                if (!graphText.empty()) text += L" [" + graphText + L"]";
            } else if (bind.key > 0) {
                text += L" [" + util::StrToWStr(util::KeyToString(bind.key)) + L"]";
            }
        }
        rows.push_back({ text, bind.active, dc.getTextSize(text, font, ts).x, anim });
    }

    for (auto it = anims.begin(); it != anims.end();) {
        if (!mgr.findBind(it->first)) it = anims.erase(it);
        else ++it;
    }

    std::ranges::sort(rows, [](Row const& a, Row const& b) { return a.width > b.width; });

    float maxW = 0.f;
    for (auto& row : rows) maxW = std::max(maxW, row.width + pad * 2.f);

    float y = 0.f;
    for (auto& row : rows) {
        float rh = (ts + pad * 2.f) * row.anim;
        float rw = row.width + pad * 2.f;
        float xOff = (1.f - row.anim) * (rw + 10.f);
        d2d::Rect rc { maxW - rw + xOff, y, maxW + xOff, y + rh };
        if (showBg) dc.fillRoundedRectangle(rc, bgColor, std::min(rh * 0.25f, 4.f));
        dc.drawText(rc, row.text, row.active ? activeColor : textColor, font, ts, DWRITE_TEXT_ALIGNMENT_CENTER,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        y += rh + rowGap;
    }

    if (rows.empty() && inEditor) {
        std::wstring text = L"Keybind List";
        maxW = dc.getTextSize(text, font, ts).x + pad * 2.f;
        d2d::Rect rc { 0.f, 0.f, maxW, ts + pad * 2.f };
        if (showBg) dc.fillRoundedRectangle(rc, bgColor, std::min(rc.getHeight() * 0.25f, 4.f));
        dc.drawText(rc, text, textColor, font, ts, DWRITE_TEXT_ALIGNMENT_CENTER,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        y = rc.bottom + rowGap;
    }

    this->rect.right = rect.left + maxW;
    this->rect.bottom = rect.top + (y > 0.f ? y - rowGap : 0.f);
}
