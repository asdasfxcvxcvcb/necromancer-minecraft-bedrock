#include "pch.h"
#include "ClickGUI.h"
#include "client/event/Eventing.h"
#include "client/event/events/RenderOverlayEvent.h"
#include "client/event/events/RendererCleanupEvent.h"
#include "client/event/events/RendererInitEvent.h"
#include "client/event/events/KeyUpdateEvent.h"
#include "client/event/events/ClickEvent.h"
#include "client/event/events/CharEvent.h"
#include "client/render/Renderer.h"
#include "client/Necromancer.h"
#include "client/input/Keyboard.h"
#include "client/feature/module/Module.h"
#include "client/feature/module/ModuleManager.h"
#include "client/feature/module/modules/visual/BlockESP.h"
#include "client/feature/module/modules/misc/ItemSwitcher.h"
#include "client/misc/ItemCatalog.h"
#include "util/DrawContext.h"
#include "../../render/asset/Assets.h"
#include "client/config/ConfigManager.h"
#include "client/misc/PlayerListManager.h"
#include "client/misc/KeybindManager.h"

#include "../ScreenManager.h"

#include <type_traits>
#include <cwctype>

#ifdef min
#undef min
#undef max
#endif
#include <client/feature/module/HUDModule.h>

#include <optional>
#include <array>
#include <shellapi.h>

using FontSelection = Renderer::FontSelection;
using RectF = d2d::Rect;

float calcAnim = 0.f;

namespace {
    static constexpr float setting_height_relative = 0.0168f; // 0.0168

    std::wstring lowercase(std::wstring text) {
        std::ranges::transform(text, text.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        return text;
    }

    bool containsSearch(std::wstring text, std::wstring const& search) {
        return lowercase(std::move(text)).find(search) != std::wstring::npos;
    }

    bool settingMatchesSearch(Setting* set, std::wstring const& search) {
        if (search.empty()) return true;
        if (containsSearch(set->getDisplayName(), search)) return true;
        if (containsSearch(set->desc(), search)) return true;
        if (containsSearch(util::StrToWStr(set->name()), search)) return true;

        if (set->enumData) {
            for (auto& entry : *set->enumData->getEntries()) {
                if (containsSearch(entry.name(), search)) return true;
                if (containsSearch(entry.desc(), search)) return true;
            }
        }

        return false;
    }

    int mouseButtonToVk(int button) {
        switch (button) {
        case 1:
            return 0x01;
        case 2:
            return 0x02;
        case 3:
            return 0x04;
        case 5:
            return 0x05;
        case 6:
            return 0x06;
        default:
            return 0;
        }
    }
}

bool ClickGUI::drawButton(D2DUtil& dc, d2d::Rect const& rc, std::wstring const& text, std::wstring const& tip,
                          bool enabled) {
    auto cursorPos = SDK::ClientInstance::get()->cursorPos;
    bool hovered = shouldSelect(rc, cursorPos);
    auto accentColor = d2d::Color(Necromancer::get().getAccentColor().getMainColor());
    auto bg = enabled ? (hovered ? accentColor : d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.11f))
                      : d2d::Color::RGB(0x70, 0x70, 0x70).asAlpha(0.11f);
    auto fg = enabled ? d2d::Colors::WHITE : d2d::Color(1.f, 1.f, 1.f, 0.32f);
    dc.fillRoundedRectangle(rc, bg, rc.getHeight() * 0.25f);
    float inset = rc.getHeight() * 0.16f;
    d2d::Rect labelRc { rc.left + inset, rc.top, rc.right - inset, rc.bottom };
    dc.drawAutoFitted(labelRc, text, fg, FontSelection::PrimaryRegular, rc.getHeight() * 0.46f,
                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (hovered && !tip.empty()) setTooltip(tip);
    return enabled && hovered && justClicked[0];
}

bool ClickGUI::isModuleInTab(Module& mod) const {
    auto name = mod.name();
    switch (modTab) {
    case COMBAT:
        return name == "AutoClicker" || name == "Aimbot" || name == "Triggerbot" || name == "ShieldBreaker" ||
               name == "Backtrack" || name == "ForwardTrack";
    case VISUALS:
        return name == "ArmorHud" || name == "BowIndicator" || name == "BlockOutline" || name == "BlockESP" || name == "CPS" || name == "ComboCounter" ||
               name == "BreakProgress" || name == "DamageIndicator" || name == "EnvironmentChanger" ||
               name == "ESP" || name == "FPS" || name == "Font" ||
               name == "Freelook" || name == "Hitboxes" || name == "ItemCounter" || name == "Fullbright" ||
               name == "Keystrokes" || name == "KeybindList" || name == "MovableCoordinates" || name == "Nickname" ||
               name == "PingDisplay" || name == "ServerDisplay" || name == "PlayerList" || name == "SpeedDisplay" ||
               name == "ThirdPersonNametag" || name == "WAILA" || name == "Zoom" || name == "OutOfViewArrows" ||
               name == "AntiObs";
    case MOVEMENT:
        return name == "ToggleSprintSneak" || name == "NoFall" || name == "AntiAFK" || name == "Fakelag" ||
               name == "Velocity" || name == "LegitScaffold";
    case MISC:
        return name == "KillNotification" || name == "SkinStealer" || name == "TextHotkey" || name == "AntiBot" ||
               name == "DisableMouseWheel" || name == "ChatSpammer" || name == "AutoBlock" || name == "ChestStealer" ||
               name == "ItemSwitcher";
    case CONFIG:
        return false;
    default:
        return false;
    }
}

void ClickGUI::refreshConfigList() {
    configList = Necromancer::getConfigManager().listUserConfigs();
    if (!selectedConfigName.empty()) {
        auto selected = selectedConfigName;
        if (std::ranges::none_of(configList, [&](auto const& cfg) { return cfg.name == selected; })) {
            selectedConfigName.clear();
        }
    }
    configListDirty = false;
}

namespace {
    std::wstring plRelativeTime(int64_t ts) {
        if (ts <= 0) return L"\x2014";
        int64_t diff = static_cast<int64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())) - ts;
        if (diff < 0) diff = 0;
        if (diff < 60) return std::to_wstring(diff) + L"s ago";
        if (diff < 3600) return std::to_wstring(diff / 60) + L"m ago";
        if (diff < 86400) return std::to_wstring(diff / 3600) + L"h ago";
        if (diff < 2592000) return std::to_wstring(diff / 86400) + L"d ago";
        return std::to_wstring(diff / 2592000) + L"mo ago";
    }
}

void ClickGUI::renderPlayerListTab(D2DUtil& dc, d2d::Rect const& area, float modulePad, bool rtl) {
    auto& mgr = PlayerListManager::get();
    auto cursorPos = SDK::ClientInstance::get()->cursorPos;
    auto accentColor = d2d::Color(Necromancer::get().getAccentColor().getMainColor());

    if (!plRenameBoxRegistered) {
        plRenameBoxRegistered = true;
        Necromancer::get().addTextBox(&plRenameBox);
    }

    float pillH = rect.getHeight() * 0.045f;
    float pillGap = pillH * 0.35f;
    float pillW = area.getWidth() * 0.15f;

    auto drawPill = [&](LocalizedString text, bool pillActive, int idx) {
        float x = rtl ? area.right - pillW - (pillW + pillGap) * idx : area.left + (pillW + pillGap) * idx;
        d2d::Rect rc { x, area.top, x + pillW, area.top + pillH };
        bool hovered = shouldSelect(rc, cursorPos);
        auto bg = pillActive ? accentColor
                             : (hovered ? d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.18f)
                                        : d2d::Color::RGB(0x44, 0x44, 0x44).asAlpha(0.22f));
        dc.fillRoundedRectangle(rc, bg, rc.getHeight() * 0.3f);
        dc.drawSingleLineFitted(rc, text, d2d::Colors::WHITE, FontSelection::PrimaryRegular, rc.getHeight() * 0.42f,
                                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (hovered && justClicked[0]) {
            playClickSound();
            return true;
        }
        return false;
    };

    auto clearRename = [&]() {
        plRenameBox.setSelected(false);
        plRenamingTag.clear();
    };

    if (drawPill(LocalizeString::get("client.ui.clickGui.playerlist.players.name"), !plTagsView, 0)) {
        plTagsView = false;
        clearRename();
    }
    if (drawPill(LocalizeString::get("client.ui.clickGui.playerlist.tags.name"), plTagsView, 1)) {
        plTagsView = true;
        clearRename();
    }

    d2d::Rect content { area.left, area.top + pillH + pillGap, area.right, area.bottom };

    scroll = std::clamp(scroll, 0.f, scrollMax);
    lerpScroll = std::lerp(lerpScroll, scroll, Necromancer::getRenderer().getDeltaTime() / 5.f);
    scrollMax = 0.f;

    std::wstring search = lowercase(searchTextBox.getText());
    float rowH = rect.getHeight() * 0.055f;

    if (!plTagsView) {
        std::vector<std::string> online = mgr.getOnlinePlayers();
        auto ciLess = [](std::string a, std::string b) {
            std::ranges::transform(a, a.begin(), ::tolower);
            std::ranges::transform(b, b.begin(), ::tolower);
            return a < b;
        };
        std::ranges::sort(online, ciLess);

        std::vector<std::string> names;
        for (auto& name : online) {
            auto wname = util::StrToWStr(name);
            if (!search.empty() && lowercase(wname).find(search) == std::wstring::npos) continue;
            names.push_back(name);
        }

        if (!plSelectedPlayer.empty() && !mgr.isOnline(plSelectedPlayer)) plSelectedPlayer.clear();
        bool detailOpen = !plSelectedPlayer.empty();
        float colGap = modulePad * 0.5f;
        int numCols = detailOpen ? 1 : std::clamp(static_cast<int>(std::ceil(names.size() / 10.0)), 1, 3);

        d2d::Rect listRect;
        d2d::Rect detailRect {};
        if (detailOpen) {
            float listW = content.getWidth() * 0.34f;
            listRect = rtl ? d2d::Rect { content.right - listW, content.top, content.right, content.bottom }
                           : d2d::Rect { content.left, content.top, content.left + listW, content.bottom };
            detailRect = rtl
                ? d2d::Rect { content.left, content.top, listRect.left - modulePad * 0.5f, content.bottom }
                : d2d::Rect { listRect.right + modulePad * 0.5f, content.top, content.right, content.bottom };
        } else {
            listRect = content;
        }

        float colW = (listRect.getWidth() - colGap * (numCols - 1)) / numCols;
        int rowsPerCol = std::max(1, static_cast<int>(std::ceil(names.size() / static_cast<double>(numCols))));
        float rowGap = rowH * 0.12f;

        dc.ctx->PushAxisAlignedClip(listRect.get(), D2D1_ANTIALIAS_MODE_ALIASED);
        modClip = listRect;

        for (size_t i = 0; i < names.size(); i++) {
            auto& name = names[i];
            auto wname = util::StrToWStr(name);
            int col = static_cast<int>(i / rowsPerCol);
            int rowIdx = static_cast<int>(i % rowsPerCol);
            float x = rtl ? listRect.right - colW * (col + 1) - colGap * col
                          : listRect.left + (colW + colGap) * col;
            float y = content.top - lerpScroll + rowIdx * (rowH + rowGap);
            d2d::Rect row { x, y, x + colW, y + rowH };
            bool selected = name == plSelectedPlayer;
            bool hovered = shouldSelect(row, cursorPos);
            auto bg = selected ? accentColor.asAlpha(0.34f)
                               : (hovered ? d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.16f)
                                          : d2d::Color::RGB(0x44, 0x44, 0x44).asAlpha(0.22f));
            dc.fillRoundedRectangle(row, bg, rowH * 0.18f);

            float dotR = rowH * 0.12f;
            float dotX = rtl ? row.right - rowH * 0.4f : row.left + rowH * 0.4f;
            dc.brush->SetColor(d2d::Color(0.25f, 1.f, 0.25f, 1.f).get());
            dc.ctx->FillEllipse(D2D1::Ellipse({ dotX, row.centerY() }, dotR, dotR), dc.brush);

            auto* colorTag = mgr.getColorTag(name);
            d2d::Color nameCol = colorTag ? d2d::Color(colorTag->r, colorTag->g, colorTag->b, 1.f)
                                          : d2d::Colors::WHITE;
            d2d::Rect nameRc = rtl
                ? d2d::Rect { row.left + rowH * 0.3f, row.top, dotX - rowH * 0.3f, row.bottom }
                : d2d::Rect { dotX + rowH * 0.3f, row.top, row.right - rowH * 0.3f, row.bottom };
            dc.drawSingleLineFitted(nameRc, wname, nameCol, FontSelection::PrimaryRegular, rowH * 0.5f,
                                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            if (hovered && justClicked[0]) {
                if (selected) plSelectedPlayer.clear();
                else plSelectedPlayer = name;
                playClickSound();
            }
        }
        scrollMax = std::max(scrollMax, std::max(0.f, rowsPerCol * (rowH + rowGap) - content.getHeight()));

        dc.ctx->PopAxisAlignedClip();
        modClip = std::nullopt;

        float pad = rowH * 0.35f;
        float dy = content.top + pad * 0.5f;
        if (detailOpen && !plSelectedPlayer.empty()) {
            auto* colorTag = mgr.getColorTag(plSelectedPlayer);
            d2d::Color nameCol = colorTag ? d2d::Color(colorTag->r, colorTag->g, colorTag->b, 1.f)
                                          : d2d::Colors::WHITE;
            auto wname = util::StrToWStr(plSelectedPlayer);

            d2d::Rect headRc { detailRect.left, dy, detailRect.right, dy + rowH * 0.85f };
            dc.drawSingleLineFitted(headRc, wname, nameCol, FontSelection::PrimarySemilight, rowH * 0.68f,
                                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            dy = headRc.bottom + pad * 0.4f;

            bool isOn = mgr.isOnline(plSelectedPlayer);
            auto& st = mgr.getStats(plSelectedPlayer);
            std::wstring statusText =
                isOn ? LocalizeString::get("client.ui.clickGui.playerlist.online.name").value()
                     : LocalizeString::get("client.ui.clickGui.playerlist.offline.name").value() + L" \x2014 " +
                           plRelativeTime(st.lastSeen);
            d2d::Color statusCol = isOn ? d2d::Color(0.25f, 1.f, 0.25f, 1.f) : d2d::Color(1.f, 1.f, 1.f, 0.45f);
            d2d::Rect statusRc { detailRect.left, dy, detailRect.right, dy + rowH * 0.55f };
            dc.drawSingleLineFitted(statusRc, statusText, statusCol, FontSelection::PrimaryRegular, rowH * 0.44f,
                                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            dy = statusRc.bottom + pad;

            float kd = st.deaths == 0 ? static_cast<float>(st.kills)
                                      : static_cast<float>(st.kills) / static_cast<float>(st.deaths);
            auto statRow = [&](std::wstring const& label, std::wstring const& value, int col, int rowIdx) {
                float cw = detailRect.getWidth() * 0.5f;
                float x = rtl ? detailRect.right - cw * (col + 1) : detailRect.left + cw * col;
                float ry = dy + rowH * 0.62f * rowIdx;
                d2d::Rect rc { x, ry, x + cw, ry + rowH * 0.62f };
                dc.drawSingleLineFitted(rc, label + L": " + value, d2d::Color(1.f, 1.f, 1.f, 0.85f),
                                        FontSelection::PrimaryRegular, rowH * 0.42f, DWRITE_TEXT_ALIGNMENT_LEADING,
                                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            };
            statRow(LocalizeString::get("client.ui.clickGui.playerlist.kills.name").value(),
                    std::to_wstring(st.kills), 0, 0);
            statRow(LocalizeString::get("client.ui.clickGui.playerlist.deaths.name").value(),
                    std::to_wstring(st.deaths), 1, 0);
            statRow(LocalizeString::get("client.ui.clickGui.playerlist.kd.name").value(),
                    util::StrToWStr(std::format("{:.2f}", kd)), 0, 1);
            statRow(LocalizeString::get("client.ui.clickGui.playerlist.encounters.name").value(),
                    std::to_wstring(st.encounters), 1, 1);
            statRow(LocalizeString::get("client.ui.clickGui.playerlist.firstSeen.name").value(),
                    plRelativeTime(st.firstSeen), 0, 2);
            statRow(LocalizeString::get("client.ui.clickGui.playerlist.lastSeen.name").value(),
                    plRelativeTime(st.lastSeen), 1, 2);
            dy += rowH * 0.62f * 3 + pad;

            d2d::Rect tagLabelRc { detailRect.left, dy, detailRect.right, dy + rowH * 0.5f };
            dc.drawSingleLineFitted(tagLabelRc, LocalizeString::get("client.ui.clickGui.playerlist.tagsLabel.name"),
                                    d2d::Color(1.f, 1.f, 1.f, 0.6f), FontSelection::PrimaryRegular, rowH * 0.4f,
                                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            dy = tagLabelRc.bottom + pad * 0.4f;

            auto assigned = mgr.getPlayerTags(plSelectedPlayer);
            float chipH = rowH * 0.9f;
            float chipGap = chipH * 0.3f;
            float chipText = chipH * 0.5f;
            float chipX = rtl ? detailRect.right : detailRect.left;
            for (auto& tag : mgr.getTags()) {
                if (tag.isDefault) continue;
                auto wt = util::StrToWStr(tag.name);
                float tw = dc.getTextSize(wt, FontSelection::PrimaryRegular, chipText, false, false,
                                          Vec2 { 10000.f, 10000.f })
                               .x +
                    chipH * 0.9f;
                float x = rtl ? chipX - tw : chipX;
                if (rtl ? (x < detailRect.left) : (x + tw > detailRect.right)) {
                    chipX = rtl ? detailRect.right : detailRect.left;
                    dy += chipH + chipGap;
                    x = rtl ? chipX - tw : chipX;
                }
                d2d::Rect chip { x, dy, x + tw, dy + chipH };
                bool tagActive = std::find(assigned.begin(), assigned.end(), tag.name) != assigned.end();
                bool isColorTag = colorTag && colorTag->name == tag.name;
                auto bg = tagActive ? d2d::Color(tag.r, tag.g, tag.b, 1.f)
                                    : d2d::Color::RGB(0x44, 0x44, 0x44).asAlpha(0.35f);
                dc.fillRoundedRectangle(chip, bg, chipH * 0.35f);
                if (isColorTag) {
                    dc.drawRoundedRectangle(chip, d2d::Colors::WHITE, chipH * 0.35f, 1.f,
                                            DrawUtil::OutlinePosition::Inside);
                }
                dc.drawSingleLineFitted(chip, wt, d2d::Colors::WHITE, FontSelection::PrimaryRegular, chipText,
                                        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                if (shouldSelect(chip, cursorPos)) {
                    setTooltip(util::StrToWStr(tag.name));
                    if (justClicked[0]) {
                        if (tagActive) mgr.removeTagFromPlayer(plSelectedPlayer, tag.name);
                        else mgr.addTagToPlayer(plSelectedPlayer, tag.name);
                        playClickSound();
                    }
                }
                chipX = rtl ? x - chipGap : x + tw + chipGap;
            }
        }
    } else {
        dc.ctx->PushAxisAlignedClip(content.get(), D2D1_ANTIALIAS_MODE_ALIASED);
        modClip = content;

        float y = content.top - lerpScroll;
        float btnW = content.getWidth() * 0.2f;
        d2d::Rect newRc = rtl ? d2d::Rect { content.right - btnW, y, content.right, y + rowH }
                              : d2d::Rect { content.left, y, content.left + btnW, y + rowH };
        if (drawButton(dc, newRc, LocalizeString::get("client.ui.clickGui.playerlist.newTag.name").value(),
                       LocalizeString::get("client.ui.clickGui.playerlist.newTag.desc").value(), true)) {
            int n = 1;
            std::string candidate;
            do {
                candidate = "Tag " + std::to_string(n++);
            } while (mgr.findTag(candidate));
            mgr.createTag(candidate, 0, 1.f, 1.f, 0.3f);
        }
        y += rowH + rowH * 0.25f;
        scrollMax = std::max(scrollMax, std::max(0.f, y - content.bottom) + lerpScroll);

        float tagRowH = rowH * 1.9f;
        bool deletedThisFrame = false;
        for (auto& tag : mgr.getTags()) {
            if (deletedThisFrame) break;
            std::string tagName = tag.name;
            d2d::Rect row { content.left, y, content.right, y + tagRowH };
            dc.fillRoundedRectangle(row, d2d::Color::RGB(0x44, 0x44, 0x44).asAlpha(0.22f), rowH * 0.2f);

            float innerPad = rowH * 0.3f;
            float sw = rowH * 0.75f;
            float topLineY = row.top + innerPad;

            d2d::Rect swatch = rtl
                ? d2d::Rect { row.right - innerPad - sw, topLineY, row.right - innerPad, topLineY + sw }
                : d2d::Rect { row.left + innerPad, topLineY, row.left + innerPad + sw, topLineY + sw };
            dc.fillRoundedRectangle(swatch, d2d::Color(tag.r, tag.g, tag.b, 1.f), sw * 0.25f);
            if (shouldSelect(swatch, cursorPos)) {
                setTooltip(LocalizeString::get("client.ui.clickGui.playerlist.tagColor.desc"));
                if (justClicked[0]) {
                    playClickSound();
                    auto& cv = tagColorValues[tagName];
                    cv = ColorValue(tag.r, tag.g, tag.b, 1.f);
                    auto& set = tagColorSettings[tagName];
                    if (!set) {
                        set = std::make_shared<Setting>("tagcolor:" + tagName, L"Tag Color", L"");
                        set->callback = [tagName](Setting& s) {
                            auto c = std::get<ColorValue>(*s.value).getMainColor();
                            PlayerListManager::get().setTagColor(tagName, c.r, c.g, c.b);
                            PlayerListManager::get().save();
                        };
                    }
                    set->value = &cv;
                    colorPicker.setting = set.get();
                    std::get<BoolValue>(colorPicker.rgbSelector) = false;
                    std::get<BoolValue>(colorPicker.forceTagSelector) = false;
                    colorPicker.dragging = false;
                    float pickerWidth = 0.2419f * rect.getWidth();
                    cPickerRect = rtl ? RectF { swatch.right - pickerWidth, swatch.bottom + 20.f, 0.f, 0.f }
                                      : RectF { swatch.left, swatch.bottom + 20.f, 0.f, 0.f };
                    colorPicker.selectedColor = &std::get<ColorValue>(cv).color1;
                    auto sCol = *colorPicker.selectedColor;
                    colorPicker.pickerColor = util::ColorToHSV({ sCol.r, sCol.g, sCol.b, sCol.a });
                    colorPicker.hueMod = colorPicker.pickerColor.h / 360.f;
                    colorPicker.svModX = colorPicker.pickerColor.s;
                    colorPicker.svModY = 1.f - colorPicker.pickerColor.v;
                    colorPicker.opacityMod = sCol.a;
                }
            }

            float delSize = sw * 0.8f;
            d2d::Rect delRc = rtl
                ? d2d::Rect { row.left + innerPad, topLineY, row.left + innerPad + delSize, topLineY + delSize }
                : d2d::Rect { row.right - innerPad - delSize, topLineY, row.right - innerPad, topLineY + delSize };
            d2d::Rect nameRc = rtl
                ? d2d::Rect { delRc.right + innerPad, topLineY, swatch.left - innerPad, topLineY + sw }
                : d2d::Rect { swatch.right + innerPad, topLineY, delRc.left - innerPad, topLineY + sw };

            if (plRenamingTag == tagName && !tag.isDefault) {
                plRenameBox.setRect(nameRc);
                plRenameBox.render(dc, nameRc.getHeight() * 0.2f, d2d::Color::RGB(0x8D, 0x8D, 0x8D).asAlpha(0.11f),
                                   D2D1::ColorF::White);
                if (justClicked[0] && !nameRc.contains(cursorPos)) plRenameBox.setSelected(false);
                if (!plRenameBox.isSelected()) {
                    mgr.renameTag(tagName, util::WStrToStr(plRenameBox.getText()));
                    if (colorPicker.setting && tagColorSettings[tagName] &&
                        colorPicker.setting == tagColorSettings[tagName].get()) {
                        colorPicker = ColorPicker();
                    }
                    tagColorSettings.erase(tagName);
                    tagColorValues.erase(tagName);
                    plRenamingTag.clear();
                }
            } else {
                dc.drawSingleLineFitted(nameRc, util::StrToWStr(tagName), d2d::Colors::WHITE,
                                        FontSelection::PrimaryRegular, sw * 0.6f, DWRITE_TEXT_ALIGNMENT_LEADING,
                                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                if (!tag.isDefault && shouldSelect(nameRc, cursorPos)) {
                    setTooltip(LocalizeString::get("client.ui.clickGui.playerlist.rename.desc"));
                    if (justClicked[0]) {
                        plRenamingTag = tagName;
                        plRenameBox.setText(util::StrToWStr(tagName));
                        plRenameBox.setCaretLocation(static_cast<int>(tagName.size()));
                        plRenameBox.setSelected(true);
                        playClickSound();
                    }
                }
            }

            if (!tag.isDefault) {
                dc.ctx->DrawBitmap(Necromancer::getAssets().xIcon.getBitmap(), delRc.get());
                if (shouldSelect(delRc, cursorPos)) {
                    setTooltip(LocalizeString::get("client.ui.clickGui.playerlist.delete.desc"));
                    if (justClicked[0]) {
                        if (colorPicker.setting && tagColorSettings[tagName] &&
                            colorPicker.setting == tagColorSettings[tagName].get()) {
                            colorPicker = ColorPicker();
                        }
                        tagColorSettings.erase(tagName);
                        tagColorValues.erase(tagName);
                        mgr.deleteTag(tagName);
                        deletedThisFrame = true;
                        playClickSound();
                    }
                }
            }

            float sliderY = topLineY + sw + innerPad * 0.7f;
            d2d::Rect sliderRow { row.left + innerPad, sliderY, row.right - innerPad, row.bottom - innerPad * 0.55f };
            float labelW = sliderRow.getWidth() * 0.18f;
            float valW = sliderRow.getWidth() * 0.16f;
            d2d::Rect labelRc = rtl
                ? d2d::Rect { sliderRow.right - labelW, sliderRow.top, sliderRow.right, sliderRow.bottom }
                : d2d::Rect { sliderRow.left, sliderRow.top, sliderRow.left + labelW, sliderRow.bottom };
            d2d::Rect valRc = rtl
                ? d2d::Rect { sliderRow.left, sliderRow.top, sliderRow.left + valW, sliderRow.bottom }
                : d2d::Rect { sliderRow.right - valW, sliderRow.top, sliderRow.right, sliderRow.bottom };
            float barGap = sliderRow.getWidth() * 0.02f;
            d2d::Rect bar = rtl
                ? d2d::Rect { valRc.right + barGap, sliderRow.top + sliderRow.getHeight() * 0.32f,
                              labelRc.left - barGap, sliderRow.bottom - sliderRow.getHeight() * 0.32f }
                : d2d::Rect { labelRc.right + barGap, sliderRow.top + sliderRow.getHeight() * 0.32f,
                              valRc.left - barGap, sliderRow.bottom - sliderRow.getHeight() * 0.32f };

            dc.drawSingleLineFitted(labelRc, LocalizeString::get("client.ui.clickGui.playerlist.priority.name"),
                                    d2d::Color(1.f, 1.f, 1.f, 0.75f), FontSelection::PrimaryRegular,
                                    sliderRow.getHeight() * 0.75f, DWRITE_TEXT_ALIGNMENT_LEADING,
                                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            if (tag.isDefault) {
                dc.fillRoundedRectangle(bar, d2d::Color::RGB(0x8D, 0x8D, 0x8D).asAlpha(0.11f), bar.getHeight() * 0.5f);
                dc.drawSingleLineFitted(valRc, L"0", d2d::Colors::WHITE, FontSelection::PrimaryRegular,
                                        sliderRow.getHeight() * 0.75f, DWRITE_TEXT_ALIGNMENT_CENTER,
                                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            } else {
                if (justClicked[0] && shouldSelect(bar, cursorPos)) {
                    plPriorityDrag = tagName;
                    playClickSound();
                }
                if (plPriorityDrag == tagName) {
                    if (!mouseButtons[0]) {
                        plPriorityDrag.clear();
                        mgr.save();
                    } else {
                        float f = rtl ? (bar.right - cursorPos.x) / bar.getWidth()
                                      : (cursorPos.x - bar.left) / bar.getWidth();
                        int val = static_cast<int>(std::round(std::clamp(f, 0.f, 1.f) * 11.f)) - 1;
                        mgr.setTagPriority(tagName, val);
                    }
                }
                float frac = (tag.priority + 1.f) / 11.f;
                dc.fillRoundedRectangle(bar, d2d::Color::RGB(0x8D, 0x8D, 0x8D).asAlpha(0.11f), bar.getHeight() * 0.5f);
                d2d::Rect fill = bar;
                if (rtl) fill.left = bar.right - bar.getWidth() * frac;
                else fill.right = bar.left + bar.getWidth() * frac;
                dc.fillRoundedRectangle(fill, accentColor, bar.getHeight() * 0.5f);
                float knobX = rtl ? fill.left : fill.right;
                dc.brush->SetColor(d2d::Color(0xB9, 0xB9, 0xB9).get());
                dc.ctx->FillEllipse(
                    D2D1::Ellipse({ knobX, bar.centerY() }, bar.getHeight() * 0.8f, bar.getHeight() * 0.8f), dc.brush);

                std::wstring valText =
                    tag.priority == -1 ? LocalizeString::get("client.ui.clickGui.playerlist.friend.name").value()
                                       : std::to_wstring(tag.priority);
                dc.drawSingleLineFitted(valRc, valText, d2d::Colors::WHITE, FontSelection::PrimaryRegular,
                                        sliderRow.getHeight() * 0.75f, DWRITE_TEXT_ALIGNMENT_CENTER,
                                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }

            y += tagRowH + rowH * 0.18f;
            scrollMax = std::max(scrollMax, std::max(0.f, (row.bottom + rowH * 0.18f) - content.bottom) + lerpScroll);
        }
        dc.ctx->PopAxisAlignedClip();
        modClip = std::nullopt;
    }

    if (scrollMax > 0.f) {
        float trackWidth = 4.f * adaptedScale;
        float trackX = rtl ? rect.left + (modulePad * 0.5f) - trackWidth : rect.right - (modulePad * 0.5f);
        d2d::Rect track { trackX, content.top, trackX + trackWidth, content.bottom };
        float thumbHeight = std::max(24.f * adaptedScale,
                                     content.getHeight() * (content.getHeight() / (content.getHeight() + scrollMax)));
        float thumbY = track.top + ((track.getHeight() - thumbHeight) * (scroll / scrollMax));
        scrollbarTrackRect = track;
        scrollbarThumbRect = { track.left, thumbY, track.right, thumbY + thumbHeight };
        if (!colorPicker.setting && justClicked[0] && scrollbarTrackRect.contains(cursorPos)) {
            draggingScrollbar = true;
            if (scrollbarThumbRect.contains(cursorPos)) {
                scrollbarDragOffset = cursorPos.y - scrollbarThumbRect.top;
            } else {
                scrollbarDragOffset = scrollbarThumbRect.getHeight() * 0.5f;
                updateScrollbarDrag(cursorPos);
                thumbY = track.top + ((track.getHeight() - thumbHeight) * (scroll / scrollMax));
                scrollbarThumbRect = { track.left, thumbY, track.right, thumbY + thumbHeight };
            }
        }
        dc.fillRoundedRectangle(scrollbarTrackRect, d2d::Color::RGB(0x55, 0x55, 0x55).asAlpha(0.28f),
                                trackWidth * 0.5f);
        dc.fillRoundedRectangle(scrollbarThumbRect,
                                (draggingScrollbar || scrollbarThumbRect.contains(cursorPos))
                                    ? accentColor
                                    : d2d::Color::RGB(0xD2, 0xD2, 0xD2).asAlpha(0.78f),
                                trackWidth * 0.5f);
    } else {
        scrollbarTrackRect = {};
        scrollbarThumbRect = {};
    }
}

void ClickGUI::renderKeybindsTab(D2DUtil& dc, d2d::Rect const& area, float modulePad, bool rtl) {
    auto& mgr = KeybindManager::get();
    auto cursorPos = SDK::ClientInstance::get()->cursorPos;
    auto accentColor = d2d::Color(Necromancer::get().getAccentColor().getMainColor());

    if (!kbRenameBoxRegistered) {
        kbRenameBoxRegistered = true;
        Necromancer::get().addTextBox(&kbRenameBox);
    }

    float rowH = rect.getHeight() * 0.055f;
    d2d::Rect content = area;

    scroll = std::clamp(scroll, 0.f, scrollMax);
    lerpScroll = std::lerp(lerpScroll, scroll, Necromancer::getRenderer().getDeltaTime() / 5.f);
    scrollMax = 0.f;

    dc.ctx->PushAxisAlignedClip(content.get(), D2D1_ANTIALIAS_MODE_ALIASED);
    modClip = content;

    float y = content.top - lerpScroll;
    float btnW = content.getWidth() * 0.2f;
    d2d::Rect newRc = rtl ? d2d::Rect { content.right - btnW, y, content.right, y + rowH }
                          : d2d::Rect { content.left, y, content.left + btnW, y + rowH };
    if (drawButton(dc, newRc, LocalizeString::get("client.ui.clickGui.keybinds.newBind.name").value(),
                   LocalizeString::get("client.ui.clickGui.keybinds.newBind.desc").value(), true)) {
        kbKindPicker = KindPicker {};
        kbKindPicker.open = true;
        kbKindPicker.justOpened = true;
        kbKindPickerRect.left = newRc.left;
        kbKindPickerRect.top = newRc.bottom + rowH * 0.12f;
        kbCapturingKey.clear();
        kbRenameBox.setSelected(false);
        kbRenamingBind.clear();
        playClickSound();
    }
    y += rowH + rowH * 0.25f;
    scrollMax = std::max(scrollMax, std::max(0.f, y - content.bottom) + lerpScroll);

    float bindRowH = rowH * 1.35f;
    float innerPad = rowH * 0.3f;
    float ctrlH = rowH * 0.75f;
    float gap = innerPad * 0.6f;
    float delSize = ctrlH * 0.82f;
    float dotW = ctrlH * 0.35f;
    bool deletedThisFrame = false;
    for (auto& bind : mgr.getBinds()) {
        if (deletedThisFrame) break;
        std::string bindName = bind.name;
        d2d::Rect row { content.left, y, content.right, y + bindRowH };
        dc.fillRoundedRectangle(row, d2d::Color::RGB(0x44, 0x44, 0x44).asAlpha(0.22f), rowH * 0.2f);

        float lineY = row.top + (row.getHeight() - ctrlH) * 0.5f;
        float avail = row.getWidth() - innerPad * 2.f - dotW - delSize - gap * 8.f;
        float cur = rtl ? row.right - innerPad : row.left + innerPad;
        auto nextSeg = [&](float w) {
            d2d::Rect rc = rtl ? d2d::Rect { cur - w, lineY, cur, lineY + ctrlH }
                               : d2d::Rect { cur, lineY, cur + w, lineY + ctrlH };
            cur += rtl ? -(w + gap) : (w + gap);
            return rc;
        };

        bool isIf = bind.kind == KeybindManager::KindIf;

        d2d::Rect dotSeg = nextSeg(dotW);
        d2d::Rect nameRc = nextSeg(avail * (isIf ? 0.18f : 0.24f));
        d2d::Rect summaryRc {};
        d2d::Rect keyRc {};
        d2d::Rect typeRc {};
        if (isIf) {
            summaryRc = nextSeg(avail * 0.18f);
        } else {
            keyRc = nextSeg(avail * 0.12f);
            typeRc = nextSeg(avail * 0.12f);
        }
        d2d::Rect visRc = nextSeg(avail * (isIf ? 0.10f : 0.11f));
        d2d::Rect actRc = nextSeg(avail * (isIf ? 0.12f : 0.13f));
        d2d::Rect parentRc = nextSeg(avail * (isIf ? 0.13f : 0.14f));
        d2d::Rect condRc = isIf ? nextSeg(avail * 0.14f) : d2d::Rect {};
        d2d::Rect editRc = nextSeg(avail * (isIf ? 0.15f : 0.14f));
        d2d::Rect delRc = rtl
            ? d2d::Rect { row.left + innerPad, lineY, row.left + innerPad + delSize, lineY + delSize }
            : d2d::Rect { row.right - innerPad - delSize, lineY, row.right - innerPad, lineY + delSize };

        float dotR = dotSeg.getHeight() * 0.13f;
        bool gateOpen = mgr.isGateOpen(bind);
        dc.brush->SetColor((bind.active      ? d2d::Color(0.25f, 1.f, 0.25f, 1.f)
                            : !gateOpen      ? d2d::Color(1.f, 0.55f, 0.15f, 0.55f)
                                             : d2d::Color(1.f, 1.f, 1.f, 0.25f))
                               .get());
        dc.ctx->FillEllipse(D2D1::Ellipse({ (dotSeg.left + dotSeg.right) * 0.5f, lineY + ctrlH * 0.5f }, dotR, dotR),
                            dc.brush);

        if (kbRenamingBind == bindName) {
            kbRenameBox.setRect(nameRc);
            kbRenameBox.render(dc, nameRc.getHeight() * 0.2f, d2d::Color::RGB(0x8D, 0x8D, 0x8D).asAlpha(0.11f),
                               D2D1::ColorF::White);
            if (justClicked[0] && !nameRc.contains(cursorPos)) kbRenameBox.setSelected(false);
            if (!kbRenameBox.isSelected()) {
                mgr.renameBind(bindName, util::WStrToStr(kbRenameBox.getText()));
                kbRenamingBind.clear();
            }
        } else {
            d2d::Color nameCol = bind.hidden ? d2d::Color(1.f, 1.f, 1.f, 0.45f) : d2d::Colors::WHITE;
            dc.drawSingleLineFitted(nameRc, util::StrToWStr(bindName), nameCol, FontSelection::PrimaryRegular,
                                    ctrlH * 0.5f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (shouldSelect(nameRc, cursorPos)) {
                setTooltip(LocalizeString::get("client.ui.clickGui.keybinds.rename.desc"));
                if (justClicked[0]) {
                    kbRenamingBind = bindName;
                    kbRenameBox.setText(util::StrToWStr(bindName));
                    kbRenameBox.setCaretLocation(static_cast<int>(bindName.size()));
                    kbRenameBox.setSelected(true);
                    playClickSound();
                }
            }
        }

        if (isIf) {
            std::wstring summaryText = KeybindManager::describeGraph(bind);
            d2d::Color summaryCol = bind.graph.empty() ? d2d::Color(1.f, 0.55f, 0.15f, 0.75f)
                                                       : d2d::Color(1.f, 1.f, 1.f, 0.62f);
            dc.drawSingleLineFitted(summaryRc, summaryText, summaryCol, FontSelection::PrimaryRegular, ctrlH * 0.45f,
                                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        } else {
            bool capturing = kbCapturingKey == bindName;
            if (capturing && justClicked[0] && !keyRc.contains(cursorPos)) kbCapturingKey.clear();
            std::wstring keyText = capturing ? L"..." : util::StrToWStr(util::KeyToString(bind.key));
            if (drawButton(dc, keyRc, keyText, LocalizeString::get("client.ui.clickGui.keybinds.key.desc").value(),
                           true)) {
                kbCapturingKey = bindName;
                playClickSound();
            }

            std::wstring typeText = bind.type == KeybindManager::TypeHold
                ? LocalizeString::get("client.ui.clickGui.keybinds.hold.name").value()
                : LocalizeString::get("client.ui.clickGui.keybinds.toggle.name").value();
            if (drawButton(dc, typeRc, typeText, LocalizeString::get("client.ui.clickGui.keybinds.type.desc").value(),
                           true)) {
                mgr.setType(bindName, bind.type == KeybindManager::TypeHold ? KeybindManager::TypeToggle
                                                                           : KeybindManager::TypeHold);
                playClickSound();
            }
        }

        std::wstring visText = bind.hidden
            ? LocalizeString::get("client.ui.clickGui.keybinds.unhide.name").value()
            : LocalizeString::get("client.ui.clickGui.keybinds.hide.name").value();
        if (drawButton(dc, visRc, visText,
                       LocalizeString::get("client.ui.clickGui.keybinds.visibility.desc").value(), true)) {
            mgr.setHidden(bindName, !bind.hidden);
            playClickSound();
        }

        std::wstring actText = bind.showOnlyWhenActive
            ? LocalizeString::get("client.ui.clickGui.keybinds.activeOnly.on.name").value()
            : LocalizeString::get("client.ui.clickGui.keybinds.activeOnly.off.name").value();
        if (drawButton(dc, actRc, actText,
                       LocalizeString::get("client.ui.clickGui.keybinds.activeOnly.desc").value(), true)) {
            mgr.setShowOnlyWhenActive(bindName, !bind.showOnlyWhenActive);
            playClickSound();
        }

        std::wstring parentText = bind.parent.empty()
            ? LocalizeString::get("client.ui.clickGui.keybinds.parent.none.name").value()
            : util::StrToWStr(bind.parent);
        if (drawButton(dc, parentRc, parentText,
                       LocalizeString::get("client.ui.clickGui.keybinds.parent.desc").value(), true)) {
            kbParentPicker = ParentPicker {};
            kbParentPicker.bind = bindName;
            kbParentPicker.justOpened = true;
            kbParentPickerRect.left = parentRc.left;
            kbParentPickerRect.top = parentRc.bottom + gap;
            kbCapturingKey.clear();
            kbRenameBox.setSelected(false);
            kbRenamingBind.clear();
            playClickSound();
        }

        if (isIf) {
            std::wstring condText = LocalizeString::get("client.ui.clickGui.keybinds.cond.open.name").value();
            if (drawButton(dc, condRc, condText,
                           LocalizeString::get("client.ui.clickGui.keybinds.cond.open.desc").value(), true)) {
                openCondCanvas(bindName);
                playClickSound();
            }
        }

        bool editingThis = kbEditingBind == bindName;
        std::wstring editText = editingThis ? LocalizeString::get("client.ui.clickGui.keybinds.done.name").value()
                                            : LocalizeString::get("client.ui.clickGui.keybinds.edit.name").value();
        if (!editingThis && !bind.edits.empty()) {
            editText += L" (" + std::to_wstring(bind.edits.size()) + L")";
        }
        if (drawButton(dc, editRc, editText, LocalizeString::get("client.ui.clickGui.keybinds.edit.desc").value(),
                       true)) {
            if (editingThis) {
                kbEditingBind.clear();
            } else {
                kbEditingBind = bindName;
                kbCapturingKey.clear();
                kbRenameBox.setSelected(false);
                kbRenamingBind.clear();
            }
            playClickSound();
        }

        dc.ctx->DrawBitmap(Necromancer::getAssets().xIcon.getBitmap(), delRc.get());
        if (shouldSelect(delRc, cursorPos)) {
            setTooltip(LocalizeString::get("client.ui.clickGui.keybinds.delete.desc"));
            if (justClicked[0]) {
                mgr.deleteBind(bindName);
                deletedThisFrame = true;
                if (kbRenamingBind == bindName) {
                    kbRenameBox.setSelected(false);
                    kbRenamingBind.clear();
                }
                if (kbCapturingKey == bindName) kbCapturingKey.clear();
                if (kbEditingBind == bindName) kbEditingBind.clear();
                playClickSound();
            }
        }

        y += bindRowH + rowH * 0.18f;
        scrollMax = std::max(scrollMax, std::max(0.f, (row.bottom + rowH * 0.18f) - content.bottom) + lerpScroll);
    }
    dc.ctx->PopAxisAlignedClip();
    modClip = std::nullopt;

    if (scrollMax > 0.f) {
        float trackWidth = 4.f * adaptedScale;
        float trackX = rtl ? rect.left + (modulePad * 0.5f) - trackWidth : rect.right - (modulePad * 0.5f);
        d2d::Rect track { trackX, content.top, trackX + trackWidth, content.bottom };
        float thumbHeight = std::max(24.f * adaptedScale,
                                     content.getHeight() * (content.getHeight() / (content.getHeight() + scrollMax)));
        float thumbY = track.top + ((track.getHeight() - thumbHeight) * (scroll / scrollMax));
        scrollbarTrackRect = track;
        scrollbarThumbRect = { track.left, thumbY, track.right, thumbY + thumbHeight };
        if (!colorPicker.setting && justClicked[0] && scrollbarTrackRect.contains(cursorPos)) {
            draggingScrollbar = true;
            if (scrollbarThumbRect.contains(cursorPos)) {
                scrollbarDragOffset = cursorPos.y - scrollbarThumbRect.top;
            } else {
                scrollbarDragOffset = scrollbarThumbRect.getHeight() * 0.5f;
                updateScrollbarDrag(cursorPos);
                thumbY = track.top + ((track.getHeight() - thumbHeight) * (scroll / scrollMax));
                scrollbarThumbRect = { track.left, thumbY, track.right, thumbY + thumbHeight };
            }
        }
        dc.fillRoundedRectangle(scrollbarTrackRect, d2d::Color::RGB(0x55, 0x55, 0x55).asAlpha(0.28f),
                                trackWidth * 0.5f);
        dc.fillRoundedRectangle(scrollbarThumbRect,
                                (draggingScrollbar || scrollbarThumbRect.contains(cursorPos))
                                    ? accentColor
                                    : d2d::Color::RGB(0xD2, 0xD2, 0xD2).asAlpha(0.78f),
                                trackWidth * 0.5f);
    } else {
        scrollbarTrackRect = {};
        scrollbarThumbRect = {};
    }
}

void ClickGUI::renderKeybindEditorOverlay(D2DUtil& dc, bool rtl) {
    if (kbEditingBind.empty()) return;
    auto* bind = KeybindManager::get().findBind(kbEditingBind);
    if (!bind) {
        kbEditingBind.clear();
        return;
    }
    auto cursorPos = SDK::ClientInstance::get()->cursorPos;
    auto accentColor = d2d::Color(Necromancer::get().getAccentColor().getMainColor());

    dc.fillRectangle(rect, d2d::Color(0.f, 0.f, 0.f, 0.35f));
    dc.drawRoundedRectangle(rect, accentColor.asAlpha(0.8f), rect.getHeight() * 0.03f, 2.f,
                            DrawUtil::OutlinePosition::Inside);

    float pad = rect.getHeight() * 0.02f;
    float bannerH = rect.getHeight() * 0.055f;
    float bannerTextSize = bannerH * 0.45f;
    std::wstring bannerText = LocalizeString::get("client.ui.clickGui.keybinds.editing.name").value() + L" " +
                              util::StrToWStr(bind->name) + L" \x2014 " +
                              LocalizeString::get("client.ui.clickGui.keybinds.editingHint.name").value();
    float bannerW = dc.getTextSize(bannerText, FontSelection::PrimarySemilight, bannerTextSize, false, false,
                                   Vec2 { 10000.f, 10000.f })
                        .x +
        bannerH;
    d2d::Rect banner { rect.left + (rect.getWidth() - bannerW) * 0.5f, rect.top + pad,
                       rect.left + (rect.getWidth() + bannerW) * 0.5f, rect.top + pad + bannerH };
    dc.fillRoundedRectangle(banner, d2d::Color::RGB(0x2E, 0x2E, 0x2E).asAlpha(0.95f), bannerH * 0.5f);
    dc.drawRoundedRectangle(banner, accentColor, bannerH * 0.5f, 1.5f, DrawUtil::OutlinePosition::Inside);
    dc.drawSingleLineFitted(banner, bannerText, d2d::Colors::WHITE, FontSelection::PrimarySemilight, bannerTextSize,
                            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    float barH = rect.getHeight() * 0.06f;
    d2d::Rect bar { rect.left + pad, rect.bottom - pad - barH, rect.right - pad, rect.bottom - pad };
    dc.fillRoundedRectangle(bar, d2d::Color::RGB(0x2E, 0x2E, 0x2E).asAlpha(0.95f), barH * 0.3f);

    float doneW = bar.getWidth() * 0.12f;
    d2d::Rect doneRc = rtl
        ? d2d::Rect { bar.left + barH * 0.2f, bar.top + barH * 0.15f, bar.left + barH * 0.2f + doneW,
                      bar.bottom - barH * 0.15f }
        : d2d::Rect { bar.right - barH * 0.2f - doneW, bar.top + barH * 0.15f, bar.right - barH * 0.2f,
                      bar.bottom - barH * 0.15f };
    if (drawButton(dc, doneRc, LocalizeString::get("client.ui.clickGui.keybinds.done.name").value(), L"", true)) {
        kbEditingBind.clear();
        playClickSound();
    }

    float chipH = doneRc.getHeight();
    float chipGap = chipH * 0.25f;
    float chipX = rtl ? bar.right - barH * 0.25f : bar.left + barH * 0.25f;
    float chipsLimit = rtl ? doneRc.right + chipGap : doneRc.left - chipGap;
    bool removedThisFrame = false;
    for (auto& edit : bind->edits) {
        if (removedThisFrame) break;
        std::wstring text = KeybindManager::describeEdit(edit) + L"  \x00D7";
        float tw = dc.getTextSize(text, FontSelection::PrimaryRegular, chipH * 0.42f, false, false,
                                  Vec2 { 10000.f, 10000.f })
                       .x +
            chipH * 0.7f;
        float x = rtl ? chipX - tw : chipX;
        if (rtl ? (x < chipsLimit) : (x + tw > chipsLimit)) break;
        d2d::Rect chip { x, doneRc.top, x + tw, doneRc.bottom };
        bool hovered = shouldSelect(chip, cursorPos);
        auto bg = hovered ? d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.22f)
                          : d2d::Color::RGB(0x8D, 0x8D, 0x8D).asAlpha(0.16f);
        dc.fillRoundedRectangle(chip, bg, chipH * 0.4f);
        dc.drawRoundedRectangle(chip, accentColor.asAlpha(0.6f), chipH * 0.4f, 1.f,
                                DrawUtil::OutlinePosition::Inside);
        dc.drawSingleLineFitted(chip, text, d2d::Colors::WHITE, FontSelection::PrimaryRegular, chipH * 0.42f,
                                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (hovered) {
            setTooltip(LocalizeString::get("client.ui.clickGui.keybinds.removeEdit.desc"));
            if (justClicked[0]) {
                KeybindManager::get().removeEdit(kbEditingBind, edit.module, edit.setting);
                removedThisFrame = true;
                playClickSound();
            }
        }
        chipX = rtl ? x - chipGap : x + tw + chipGap;
    }

    std::string editLabel = kbHoverLabel;
    if (editLabel.empty() && !bind->edits.empty()) {
        editLabel = bind->edits.back().module + "." + bind->edits.back().setting;
    }
    if (!editLabel.empty()) {
        std::wstring wlabel = util::StrToWStr(editLabel);
        float pillH = barH * 0.7f;
        float pillTextSize = pillH * 0.42f;
        float pillTextW = dc.getTextSize(wlabel, FontSelection::PrimaryRegular, pillTextSize, false, false,
                                         Vec2 { 10000.f, 10000.f })
                              .x;
        float pillW = pillTextW + pillH * 0.9f;
        float pillCX = rect.left + rect.getWidth() * 0.5f;
        d2d::Rect pill { pillCX - pillW * 0.5f, bar.top - pad * 0.6f - pillH, pillCX + pillW * 0.5f,
                         bar.top - pad * 0.6f };
        dc.fillRoundedRectangle(pill, d2d::Color::RGB(0x2E, 0x2E, 0x2E).asAlpha(0.95f), pillH * 0.5f);
        dc.drawRoundedRectangle(pill, accentColor, pillH * 0.5f, 1.f, DrawUtil::OutlinePosition::Inside);
        dc.drawSingleLineFitted(pill, wlabel, d2d::Colors::WHITE, FontSelection::PrimaryRegular, pillTextSize,
                                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

void ClickGUI::renderConfigTab(D2DUtil& dc, d2d::Rect const& listRect, float modulePad, float padFromSearchBar, bool rtl) {
    if (configListDirty) refreshConfigList();

    auto cursorPos = SDK::ClientInstance::get()->cursorPos;
    auto accentColor = d2d::Color(Necromancer::get().getAccentColor().getMainColor());
    bool hasSelected = !selectedConfigName.empty();
    bool selectedProtected = false;
    if (hasSelected) {
        for (auto const& cfg : configList) {
            if (cfg.name == selectedConfigName) selectedProtected = cfg.protectedConfig;
        }
    }
    float actionHeight = rect.getHeight() * 0.052f;
    float actionGap = modulePad * 0.35f;
    float folderSize = actionHeight;
    float folderGap = actionGap;
    float actionWidth = (listRect.getWidth() - folderSize - folderGap - actionGap * 3.f) / 4.f;
    float x = rtl ? listRect.right - actionWidth : listRect.left;
    auto actionRect = [&](int index) {
        float off = (actionWidth + actionGap) * static_cast<float>(index);
        if (rtl) return d2d::Rect { x - off, listRect.top, x + actionWidth - off, listRect.top + actionHeight };
        return d2d::Rect { x + off, listRect.top, x + off + actionWidth, listRect.top + actionHeight };
    };
    auto folderRect = [&]() {
        if (rtl) {
            return d2d::Rect { listRect.left, listRect.top, listRect.left + folderSize, listRect.top + actionHeight };
        }
        return d2d::Rect { listRect.right - folderSize, listRect.top, listRect.right, listRect.top + actionHeight };
    }();

    auto primaryText = hasSelected ? LocalizeString::get("client.ui.clickGui.config.load.name").value()
                                  : LocalizeString::get("client.ui.clickGui.config.add.name").value();
    auto primaryDesc = hasSelected ? LocalizeString::get("client.ui.clickGui.config.load.desc").value()
                                  : LocalizeString::get("client.ui.clickGui.config.add.desc").value();
    bool clickedConfigUi = false;
    if (justClicked[0]) {
        for (int i = 0; i < 4; ++i) {
            if (actionRect(i).contains(cursorPos)) clickedConfigUi = true;
        }
        if (folderRect.contains(cursorPos)) clickedConfigUi = true;
    }

    {
        bool folderHovered = shouldSelect(folderRect, cursorPos);
        auto folderBg = folderHovered ? d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.18f)
                                      : d2d::Color::RGB(0x44, 0x44, 0x44).asAlpha(0.22f);
        dc.fillRoundedRectangle(folderRect, folderBg, folderSize * 0.22f);
        if (auto* folderBmp = Necromancer::getAssets().folderIcon.getBitmap()) {
            float pad = folderSize * 0.22f;
            d2d::Rect iconRect { folderRect.left + pad, folderRect.top + pad, folderRect.right - pad,
                                 folderRect.bottom - pad };
            dc.ctx->DrawBitmap(folderBmp, iconRect.get());
        }
        if (folderHovered) {
            setTooltip(LocalizeString::get("client.ui.clickGui.config.openFolder.desc"));
            if (justClicked[0]) {
                auto configsDir = Necromancer::getConfigManager().getUserPath();
                std::error_code ec;
                std::filesystem::create_directories(configsDir, ec);
                if (std::filesystem::exists(configsDir, ec)) {
                    ShellExecuteW(nullptr, L"explore", configsDir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                playClickSound();
            }
        }
    }

    if (drawButton(dc, actionRect(0), primaryText, primaryDesc, true)) {
        if (hasSelected) {
            if (Necromancer::getConfigManager().loadAndApply(selectedConfigName)) {
                configStatusText = LocalizeString::get("client.ui.clickGui.config.loaded.name");
            } else {
                configStatusText = LocalizeString::get("client.ui.clickGui.config.failed.name");
            }
            configStatusTimer = 2.f;
        } else {
            configInputOpen = true;
            configRenameMode = false;
            configNameTextBox.setText(L"");
            configNameTextBox.setCaretLocation(0);
            configNameTextBox.setSelected(true);
        }
        playClickSound();
    }

    if (drawButton(dc, actionRect(1), LocalizeString::get("client.ui.clickGui.config.rename.name"),
               hasSelected ? (selectedProtected ? LocalizeString::get("client.ui.clickGui.config.autoConfig.desc").value()
                                                 : LocalizeString::get("client.ui.clickGui.config.rename.desc").value())
                           : LocalizeString::get("client.ui.clickGui.config.selectFirst.desc").value(),
               hasSelected && !selectedProtected)) {
        configInputOpen = true;
        configRenameMode = true;
        configNameTextBox.setText(selectedConfigName);
        configNameTextBox.setCaretLocation(static_cast<int>(selectedConfigName.size()));
        configNameTextBox.setSelected(true);
        playClickSound();
    }

    if (drawButton(dc, actionRect(2), LocalizeString::get("client.ui.clickGui.config.delete.name"),
               hasSelected ? (selectedProtected ? LocalizeString::get("client.ui.clickGui.config.autoConfig.desc").value()
                                                 : LocalizeString::get("client.ui.clickGui.config.delete.desc").value())
                           : LocalizeString::get("client.ui.clickGui.config.selectFirst.desc").value(),
               hasSelected && !selectedProtected)) {
        if (Necromancer::getConfigManager().deleteUserConfig(selectedConfigName)) {
            selectedConfigName.clear();
            configListDirty = true;
            configStatusText = LocalizeString::get("client.ui.clickGui.config.deleted.name");
        } else {
            configStatusText = LocalizeString::get("client.ui.clickGui.config.failed.name");
        }
        configStatusTimer = 2.f;
        playClickSound();
    }

    if (drawButton(dc, actionRect(3), LocalizeString::get("client.ui.clickGui.config.update.name"),
               hasSelected ? LocalizeString::get("client.ui.clickGui.config.update.desc").value()
                           : LocalizeString::get("client.ui.clickGui.config.selectFirst.desc").value(),
               hasSelected)) {
        if (Necromancer::getConfigManager().saveTo(selectedConfigName)) {
            configListDirty = true;
            configStatusText = LocalizeString::get("client.ui.clickGui.config.updated.name");
        } else {
            configStatusText = LocalizeString::get("client.ui.clickGui.config.failed.name");
        }
        configStatusTimer = 2.f;
        playClickSound();
    }

    float listTop = listRect.top + actionHeight + padFromSearchBar * 0.75f;
    if (configInputOpen) {
        float inputHeight = actionHeight;
        d2d::Rect inputRect { listRect.left, listTop, listRect.right - actionWidth - actionGap, listTop + inputHeight };
        d2d::Rect okRect { inputRect.right + actionGap, listTop, inputRect.right + actionGap + actionWidth * 0.48f,
                           listTop + inputHeight };
        d2d::Rect cancelRect { okRect.right + actionGap * 0.35f, listTop, listRect.right, listTop + inputHeight };
        if (rtl) {
            inputRect = { listRect.left + actionWidth + actionGap, listTop, listRect.right, listTop + inputHeight };
            cancelRect = { listRect.left, listTop, listRect.left + actionWidth * 0.48f, listTop + inputHeight };
            okRect = { cancelRect.right + actionGap * 0.35f, listTop, inputRect.left - actionGap, listTop + inputHeight };
        }

        if (justClicked[0]) {
            if (inputRect.contains(cursorPos) || okRect.contains(cursorPos) || cancelRect.contains(cursorPos)) {
                clickedConfigUi = true;
            }
        }
        configNameTextBox.setRect(inputRect);
        configNameTextBox.render(dc, inputHeight * 0.25f, d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.11f),
                                 d2d::Colors::WHITE);
        dc.drawRoundedRectangle(inputRect, d2d::Color(1.f, 1.f, 1.f, configNameTextBox.isSelected() ? 0.55f : 0.18f),
                                inputHeight * 0.25f, 1.f, DrawUtil::OutlinePosition::Inside);

        if (drawButton(dc, okRect, LocalizeString::get("client.ui.clickGui.config.ok.name"), L"", true)) {
            auto newName = configNameTextBox.getText();
            bool result = configRenameMode ? Necromancer::getConfigManager().renameUserConfig(selectedConfigName, newName)
                                           : Necromancer::getConfigManager().saveTo(newName);
            if (result) {
                selectedConfigName = newName;
                configInputOpen = false;
                configNameTextBox.setSelected(false);
                configListDirty = true;
                configStatusText = configRenameMode ? LocalizeString::get("client.ui.clickGui.config.renamed.name").value()
                                                    : LocalizeString::get("client.ui.clickGui.config.added.name").value();
            } else {
                configStatusText = LocalizeString::get("client.ui.clickGui.config.failed.name");
            }
            configStatusTimer = 2.f;
            playClickSound();
        }

        if (drawButton(dc, cancelRect, LocalizeString::get("client.ui.clickGui.config.cancel.name"), L"", true)) {
            configInputOpen = false;
            configNameTextBox.setSelected(false);
            playClickSound();
        }

        listTop += inputHeight + padFromSearchBar * 0.75f;
    }

    if (configStatusTimer > 0.f) {
        configStatusTimer -= Necromancer::getRenderer().getDeltaTime();
        d2d::Rect statusRect { listRect.left, listTop, listRect.right, listTop + actionHeight * 0.65f };
        dc.drawSingleLineFitted(statusRect, configStatusText, d2d::Color(1.f, 1.f, 1.f, 0.72f),
                                FontSelection::PrimaryRegular, statusRect.getHeight() * 0.55f,
                                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        listTop = statusRect.bottom + padFromSearchBar * 0.35f;
    }

    d2d::Rect clip { listRect.left, listTop, listRect.right, listRect.bottom };
    dc.ctx->PushAxisAlignedClip(clip.get(), D2D1_ANTIALIAS_MODE_ALIASED);
    modClip = clip;
    float rowHeight = rect.getHeight() * 0.064f;
    float rowGap = rowHeight * 0.18f;
    float y = listTop - lerpScroll;
    scroll = std::clamp(scroll, 0.f, scrollMax);
    lerpScroll = std::lerp(lerpScroll, scroll, Necromancer::getRenderer().getDeltaTime() / 5.f);
    scrollMax = 0.f;

    std::wstring search = lowercase(searchTextBox.getText());
    int rendered = 0;
    for (auto const& cfg : configList) {
        if (cfg.protectedConfig) continue;
        if (!search.empty() && !containsSearch(cfg.name, search)) continue;
        d2d::Rect row { listRect.left, y, listRect.right, y + rowHeight };
        bool selected = cfg.name == selectedConfigName;
        bool hovered = shouldSelect(row, cursorPos);
        auto bg = selected ? accentColor.asAlpha(0.34f)
                           : (hovered ? d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.16f)
                                      : d2d::Color::RGB(0x44, 0x44, 0x44).asAlpha(0.22f));
        dc.fillRoundedRectangle(row, bg, rowHeight * 0.22f);
        dc.drawSingleLineFitted({ row.left + rowHeight * 0.45f, row.top, row.right - rowHeight * 0.45f, row.bottom },
                                cfg.name, cfg.exists ? d2d::Colors::WHITE : d2d::Color(1.f, 1.f, 1.f, 0.38f),
                                FontSelection::PrimaryRegular, rowHeight * 0.38f, DWRITE_TEXT_ALIGNMENT_LEADING,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (hovered) {
            setTooltip(cfg.protectedConfig ? LocalizeString::get("client.ui.clickGui.config.autoConfig.desc")
                                           : LocalizeString::get("client.ui.clickGui.config.select.desc"));
            if (justClicked[0]) {
                clickedConfigUi = true;
                selectedConfigName = selected ? L"" : cfg.name;
                configInputOpen = false;
                configNameTextBox.setSelected(false);
                playClickSound();
            }
        }
        y += rowHeight + rowGap;
        scrollMax = std::max(scrollMax, std::max(0.f, y + padFromSearchBar - listRect.bottom + lerpScroll));
        rendered++;
    }

    if (rendered == 0) {
        dc.drawSingleLineFitted(clip, LocalizeString::get("client.ui.clickGui.config.empty.name"),
                                d2d::Color(1.f, 1.f, 1.f, 0.45f), FontSelection::PrimaryRegular,
                                rowHeight * 0.34f, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    if (justClicked[0] && !clickedConfigUi) {
        selectedConfigName.clear();
    }

    dc.ctx->PopAxisAlignedClip();
    modClip = std::nullopt;
}

ClickGUI::ClickGUI() {
    this->key = Necromancer::get().getMenuKey();
    this->key2 = Necromancer::get().getSecondaryMenuKey();

    Necromancer::get().addTextBox(&this->searchTextBox);
    Necromancer::get().addTextBox(&this->configNameTextBox);

    Eventing::get().listen<RenderOverlayEvent>(this, (EventListenerFunc)&ClickGUI::onRender, 1, true);
    Eventing::get().listen<RendererCleanupEvent>(this, (EventListenerFunc)&ClickGUI::onCleanup, 1, true);
    Eventing::get().listen<RendererInitEvent>(this, (EventListenerFunc)&ClickGUI::onInit, 1, true);
    Eventing::get().listen<KeyUpdateEvent>(this, (EventListenerFunc)&ClickGUI::onKey, 1);
    Eventing::get().listen<ClickEvent>(this, (EventListenerFunc)&ClickGUI::onClick, 5);
}

void ClickGUI::updateScrollbarDrag(Vec2 const& mouse) {
    if (!draggingScrollbar) return;
    if (!mouseButtons[0] || scrollMax <= 0.f || scrollbarTrackRect.getHeight() <= scrollbarThumbRect.getHeight()) {
        draggingScrollbar = false;
        return;
    }

    float availableTrack = scrollbarTrackRect.getHeight() - scrollbarThumbRect.getHeight();
    float thumbTop = std::clamp(mouse.y - scrollbarDragOffset, scrollbarTrackRect.top,
                                scrollbarTrackRect.bottom - scrollbarThumbRect.getHeight());
    float normalized = (thumbTop - scrollbarTrackRect.top) / availableTrack;
    scroll = std::clamp(normalized * scrollMax, 0.f, scrollMax);
}

void ClickGUI::onRender(Event&) {
    kbHoverLabel.clear();
    static std::vector<ModuleLike> mods = {};

    static size_t lastCount = 0;

    if (shouldRebuildModLikes) {
        shouldRebuildModLikes = false;
        mods.clear();
    }

    if (mods.empty() || (Necromancer::getModuleManager().size() != lastCount)) {
        lastCount = Necromancer::getModuleManager().size();
        mods.clear();

        Necromancer::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
            if (mod->isVisible()) {
                ModuleLike container { mod->getDisplayName(), mod->desc(), mod };
                mods.emplace_back(container);
            }
            return false;
        });
    }

    {
        auto scn = Necromancer::getScreenManager().getActiveScreen();
        if (!isActive() && (calcAnim < 0.03f)) {
            calcAnim = 0.f;
            return;
        }
        if (scn) {
            auto scnName = scn->get().getName();
            if (scnName != this->getName()) {
                calcAnim = 0.f;
                return;
            }
        }
    }

    bool shouldArrow = true;

    if (colorPicker.setting) {
        addLayer(cPickerRect);
    }

    if (blockPicker.mod) {
        addLayer(bPickerRect);
    }

    if (!kbParentPicker.bind.empty()) {
        addLayer(kbParentPickerRect);
    }

    if (kbKindPicker.open) {
        addLayer(kbKindPickerRect);
    }

    if (condCanvas.open) {
        addLayer(condCanvasRect);
    }

    D2DUtil dc;
    if (!isActive()) justClicked = { false, false, false };
    bool condCanvasClick = condCanvas.open && justClicked[0];
    if (condCanvas.open) justClicked[0] = false;
    dc.ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    Vec2& cursorPos = SDK::ClientInstance::get()->cursorPos;
    auto accentColor = d2d::Color(Necromancer::get().getAccentColor().getMainColor());
    updateScrollbarDrag(cursorPos);
    if (!mouseButtons[0]) draggingScrollbar = false;

    // auto& ev = reinterpret_cast<RenderOverlayEvent&>(evGeneric);
    auto& rend = Necromancer::getRenderer();
    auto ss = rend.getScreenSize();

    adaptedScale = ss.width / 1920.f;

    float guiX = ss.width / 4.f;
    float guiY = ss.height / 4.f;
    {
        float totalWidth = ss.height * (16.f / 9.f);
        ;

        float realGuiX = totalWidth / 2.f;

        guiX = (ss.width / 2.25f) - (realGuiX / 2.f);
        guiY = (ss.height / 5.f);
    }

    rect = { guiX, guiY, ss.width - guiX, ss.height - guiY };
    float guiWidth = rect.getWidth();
    const bool rtl = Necromancer::get().getL10nData().isSelectedLanguageRightToLeft();

    if (blockPicker.mod && isActive()) {
        auto* preFrame = rend.getCopiedBitmap();
        (void)preFrame;
    }

    if (Necromancer::get().getMenuBlur())
        dc.drawGaussianBlur(Necromancer::get().getMenuBlur().value() * (isActive() ? 1.f : calcAnim));

    // Animation
    D2D1::Matrix3x2F oTransform;
    D2D1::Matrix3x2F currentMatr;
    if (isActive()) {
        dc.ctx->GetTransform(&oTransform);

        dc.ctx->SetTransform(
            D2D1::Matrix3x2F::Scale({ calcAnim, calcAnim }, D2D1_POINT_2F(rect.center().x, rect.center().y)));
        dc.ctx->GetTransform(&currentMatr);
    }
    calcAnim = std::lerp(calcAnim, isActive() ? 1.f : 0.f, Necromancer::getRenderer().getDeltaTime() * 0.2f);

    d2d::Color outline = d2d::Color::RGB(0, 0, 0);
    outline.a = 0.28f;
    d2d::Color rcColor = d2d::Color::RGB(0x7, 0x7, 0x7);
    rcColor.a = 0.75f;
    rect.round();

    if (!isActive()) return;
    // Shadow effect stuff
    auto shadowEffect = Necromancer::getRenderer().getShadowEffect();
    shadowEffect->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(0.f, 0.f, 0.f, 0.1f));
    auto affineTransformEffect = Necromancer::getRenderer().getAffineTransformEffect();

    D2D1::Matrix3x2F mat = D2D1::Matrix3x2F::Translation(10.f * adaptedScale, 5.f * adaptedScale);
    affineTransformEffect->SetInputEffect(0, shadowEffect);
    affineTransformEffect->SetValue(D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX, mat);
    // Shadow effect bitmap
    auto myBitmap = rend.getBitmap();
    //

    // Menu Rect
    dc.fillRoundedRectangle(rect, rcColor, 19.f * adaptedScale);
    dc.drawRoundedRectangle(rect, outline, 19.f * adaptedScale, 4.f * adaptedScale, DrawUtil::OutlinePosition::Outside);

    float offX = 0.01689f * rect.getWidth();
    float offY = 0.03191f * rect.getHeight();
    float imgSize = 0.05338f * rect.getWidth();

    RectF logoRect = d2d::rectFromStart(rect, offX, rect.top + offY, imgSize, imgSize, rtl);

    // Necromancer Logo + text
    {
        {
            auto bmp = Necromancer::getAssets().necromancerLogo.getBitmap();

            D2D1::Matrix3x2F oMat;
            auto sz = Necromancer::getRenderer().getScreenSize();

            D2D1::Matrix3x2F m;

            // dc.ctx->GetTransform(&m);
            // dc.ctx->SetTransform(D2D1::Matrix3x2F::Scale(41.f / sz.width, 41.f / sz.height) *
            // D2D1::Matrix3x2F::Translation(logoRect.left, logoRect.top) * m);
            dc.ctx->DrawBitmap(bmp, logoRect, 1.f);
            // dc.ctx->DrawImage(compositeEffect.Get(), { 0.f, 0.f });
            // dc.ctx->SetTransform(m);
        }

        // Necromancer Text
        float realLogoHeight = rect.getHeight() * 0.077921f;
        float titleSize = 25.f * adaptedScale;
        std::wstring titleText = L"\x202ANecromancer Client\x202C";
        float titleGap = 9.f * adaptedScale;
        float titleWidth = 500.f * adaptedScale;
        RectF titleRect = rtl ? RectF { logoRect.left - titleGap - titleWidth, logoRect.top, logoRect.left - titleGap,
                                        logoRect.top + realLogoHeight }
                              : RectF { logoRect.right + titleGap, logoRect.top, logoRect.right + titleGap + titleWidth,
                                        logoRect.top + realLogoHeight };
        dc.drawText(titleRect, titleText, d2d::Color(1.f, 1.f, 1.f, 1.f), FontSelection::PrimaryLight, titleSize,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    }

    // X button / other menus
    {
        // x btn calc
        float xOffs = rect.getWidth() * 0.02217f;
        float yOffs = rect.getHeight() * 0.04432f;

        float xWidth = rect.getWidth() * 0.02323f;
        float xHeight = xWidth; // rect.getHeight() * 0.04078f;

        RectF xRect = d2d::rectFromEnd(rect, xOffs, rect.top + yOffs, xWidth, xHeight, rtl);

        auto bmp = Necromancer::getAssets().xIcon.getBitmap();
        dc.ctx->DrawBitmap(bmp, xRect, 1.f);

        if (shouldSelect(xRect, cursorPos)) {
            if (justClicked[0]) {
                playClickSound();
                close();
            }
        }

        float betw = rect.getWidth() * 0.01795f;
        if (tab == SETTINGS) {
            RectF backArrowRect =
                rtl ? RectF { xRect.right + betw, xRect.top, xRect.right + betw + xWidth, xRect.bottom }
                    : RectF { xRect.left - betw - xWidth, xRect.top, xRect.left - betw, xRect.bottom };
            { dc.drawBitmapMirroredX(Necromancer::getAssets().arrowBackIcon.getBitmap(), backArrowRect, rtl); }
            if (shouldSelect(backArrowRect, cursorPos)) {
                if (justClicked[0]) {
                    playClickSound();
                    this->tab = MODULES;
                }
            }
        } else if (tab == MODULES) {
            RectF hudEditRect;
            {
                float hudEditWidth = rect.getWidth() * 0.02851f;
                float hudEditHeight = rect.getHeight() * 0.04432f;
                hudEditRect = rtl ? RectF { xRect.right + betw, xRect.bottom - hudEditHeight,
                                            xRect.right + betw + hudEditWidth, xRect.bottom }
                                  : RectF { xRect.left - betw - hudEditWidth, xRect.bottom - hudEditHeight,
                                            xRect.left - betw, xRect.bottom };

                dc.ctx->DrawBitmap(Necromancer::getAssets().hudEditIcon.getBitmap(), hudEditRect);

                if (shouldSelect(hudEditRect, cursorPos)) {
                    setTooltip(LocalizeString::get("client.ui.clickGui.openHudEditor.desc"));
                    if (justClicked[0]) {
                        playClickSound();
                        close();
                        Necromancer::getScreenManager().showScreen<HUDEditor>(true);
                    }
                }
            }

            // settings button
            RectF settingsRect;
            {
                float setSize = rect.getWidth() * 0.02745f;
                settingsRect = rtl ? RectF { hudEditRect.right + betw, hudEditRect.bottom - setSize,
                                             hudEditRect.right + betw + setSize, hudEditRect.bottom }
                                   : RectF { hudEditRect.left - betw - setSize, hudEditRect.bottom - setSize,
                                             hudEditRect.left - betw, hudEditRect.bottom };

                if (shouldSelect(settingsRect, cursorPos)) {
                    setTooltip(LocalizeString::get("client.ui.clickGui.openSettings.desc"));
                    if (justClicked[0]) {
                        playClickSound();
                        this->tab = SETTINGS;
                    }
                }

                dc.ctx->DrawBitmap(Necromancer::getAssets().cogIcon.getBitmap(), settingsRect);
            }
        }
    }

    // Search Bar + tabs
    RectF searchRect {};
    {
        float gaps = guiWidth * 0.009f;
        float gapY = rect.getHeight() * 0.0175f;

        // prototype height = 564

        float searchWidth = guiWidth * 0.25f;
        float searchHeight = 0.0425f * rect.getHeight();
        float searchRound = searchHeight * 0.416f;

        searchRect = d2d::rectFromStart(rect, offX, logoRect.bottom + gapY, searchWidth, searchHeight, rtl);
        auto searchCol = d2d::Color::RGB(0x70, 0x70, 0x70).asAlpha(0.28f);

        if (shouldSelect(searchRect, cursorPos)) {
            cursor = Cursor::IBeam;
            shouldArrow = false;
        }

        if (justClicked[0]) {
            if (shouldSelect(searchRect, cursorPos)) {
                searchTextBox.setSelected(true);
                activeSetting = nullptr;
                clearSettingBoxFocus();
            } else {
                searchTextBox.setSelected(false);
            }
        }

        {
            dc.ctx->SetTarget(shadowBitmap.Get());
            dc.ctx->Clear();

            dc.ctx->SetTransform(currentMatr);

            D2D1_ROUNDED_RECT rr;
            rr.radiusX = searchRound;
            rr.radiusY = searchRound;
            rr.rect = searchRect.get();
            rend.getSolidBrush()->SetColor(searchCol.get());
            dc.ctx->FillRoundedRectangle(rr, rend.getSolidBrush());

            // Shadow

            D2D1::Matrix3x2F matr = D2D1::Matrix3x2F::Translation(5 * adaptedScale, 5 * adaptedScale);
            affineTransformEffect->SetValue(D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX, matr);
            shadowEffect->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(0.f, 0.f, 0.f, 0.4f));
            shadowEffect->SetValue(D2D1_SHADOW_PROP_OPTIMIZATION, D2D1_SHADOW_OPTIMIZATION_SPEED);

            shadowEffect->SetInput(0, shadowBitmap.Get());
            compositeEffect->SetInputEffect(0, affineTransformEffect);
            compositeEffect->SetInput(1, shadowBitmap.Get());
            {
                std::wstring searchStr = L"";
                if (searchTextBox.getText().empty() && !searchTextBox.isSelected()) {
                    if (this->tab == SETTINGS) {
                        searchStr += LocalizeString::get("client.ui.clickGui.searchSettings.name").value();
                    } else if (this->tab == MODULES) {
                        searchStr += LocalizeString::get("client.ui.clickGui.search.name").value();
                    }
                } else {
                    searchStr = searchTextBox.getText();
                }
                Vec2 ts = dc.getTextSize(searchTextBox.getText().substr(0, searchTextBox.getCaretLocation()),
                                         Renderer::FontSelection::PrimaryRegular, searchRect.getHeight() / 2.f);
                RectF textSearchRect =
                    rtl ? RectF { searchRect.left + 5.f, searchRect.top,
                                  searchRect.right - 5.f - searchRect.getHeight(), searchRect.bottom }
                        : RectF { searchRect.left + 5.f + searchRect.getHeight(), searchRect.top,
                                  searchRect.right - 5.f, searchRect.bottom };
                float blinkerX = rtl ? textSearchRect.right - ts.x - 2.f : textSearchRect.left + ts.x;
                d2d::Rect blinkerRect = { blinkerX, searchRect.top + 3.f, blinkerX + 2.f, searchRect.bottom - 3.f };
                if (searchTextBox.isSelected() && searchTextBox.shouldBlink())
                    dc.fillRectangle(blinkerRect, d2d::Color::RGB(0xB9, 0xB9, 0xB9));
                dc.drawSingleLineFitted(textSearchRect, searchStr, d2d::Color::RGB(0xB9, 0xB9, 0xB9),
                                        FontSelection::PrimaryRegular, searchRect.getHeight() / 2.f,
                                        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
                RectF searchIconRect =
                    rtl ? RectF { searchRect.right - searchRect.getHeight() + 3.f, searchRect.top + 6.f,
                                  searchRect.right - 10.f, searchRect.top + searchRect.getHeight() - 6.f }
                        : RectF { searchRect.left + 10.f, searchRect.top + 6.f,
                                  searchRect.left - 3.f + searchRect.getHeight(),
                                  searchRect.top + searchRect.getHeight() - 6.f };
                dc.ctx->DrawBitmap(Necromancer::getAssets().searchIcon.getBitmap(), searchIconRect);
            }

            dc.ctx->SetTarget(myBitmap);
        }

        if (tab == SETTINGS) {
            // actual settings
            auto& settings = Necromancer::getSettings();
            std::wstring settingSearch = lowercase(searchTextBox.getText());

            float settingWidth = rect.getWidth() / 3.f;
            float padToSettings = 0.04787f * rect.getHeight();
            float settingRowHeight = rect.getWidth() * setting_height_relative;
            float settingsBottom = rect.bottom - settingRowHeight * 1.3f;
            auto hasSettingRoom = [settingsBottom, settingRowHeight](Vec2 const& settingPos) {
                return settingPos.y + settingRowHeight <= settingsBottom;
            };
            float startColumnX = d2d::logicalColumnX(rect, offX, settingWidth, rtl);
            // float settings
            Vec2 setPos = { startColumnX, searchRect.bottom + padToSettings };
            {
                // go through all float settings
                settings.forEach([&](std::shared_ptr<Setting> set) {
                    if (hasSettingRoom(setPos)) {
                        if (set->visible && settingMatchesSearch(set.get(), settingSearch) &&
                            set->shouldRender(settings) &&
                            set->value->index() ==
                                (size_t)Setting::Type::Float /* || set->value->index() == Setting::Type::Int*/) {
                            setPos.y = drawSetting(set.get(), &settings, setPos, dc, settingWidth, 0.35f) +
                                       (setting_height_relative * rect.getHeight());
                        }
                    }
                });
            }

            // key/enum settings
            setPos.y += padToSettings;
            {
                // go through all enum settings
                settings.forEach([&](std::shared_ptr<Setting> set) {
                    if (hasSettingRoom(setPos)) {
                        if (set->visible && settingMatchesSearch(set.get(), settingSearch) &&
                            set->shouldRender(settings) &&
                            (set->value->index() == (size_t)Setting::Type::Key ||
                             set->value->index() == (size_t)Setting::Type::Enum ||
                             set->value->index() == (size_t)Setting::Type::Color ||
                             set->value->index() == (size_t)Setting::Type::Text)) {
                            setPos.y = drawSetting(set.get(), &settings, setPos, dc, settingWidth) +
                                       (setting_height_relative * rect.getHeight());
                        }
                    }
                });
            }

            // bool settings
            setPos = { d2d::logicalColumnX(rect, rect.getWidth() * (1.3f / 3.f), settingWidth, rtl),
                       searchRect.bottom + padToSettings };
            {
                // go through all bool settings
                settings.forEach([&](std::shared_ptr<Setting> set) {
                    if (hasSettingRoom(setPos)) {
                        if (set->visible && settingMatchesSearch(set.get(), settingSearch) &&
                            set->shouldRender(settings) &&
                            set->value->index() ==
                                (size_t)Setting::Type::Bool /* || set->value->index() == Setting::Type::Enum*/) {
                            setPos.y = drawSetting(set.get(), &settings, setPos, dc, settingWidth) +
                                       (setting_height_relative * rect.getHeight());
                        }
                    }
                });
            }
        } else if (tab == MODULES) {
            static std::vector<std::tuple<std::string, ClickGUI::ModTab, d2d::Color, float>> modTabs = {
                { "client.ui.clickGui.tab.aimbot.name", COMBAT, searchCol, 0.f },
                { "client.ui.clickGui.tab.visuals.name", VISUALS, searchCol, 0.f },
                { "client.ui.clickGui.tab.movement.name", MOVEMENT, searchCol, 0.f },
                { "client.ui.clickGui.tab.misc.name", MISC, searchCol, 0.f },
                { "client.ui.clickGui.tab.playerlist.name", PLAYERLIST, searchCol, 0.f },
                { "client.ui.clickGui.tab.keybinds.name", KEYBINDS, searchCol, 0.f },
                { "client.ui.clickGui.tab.config.name", CONFIG, searchCol, 0.f }
            };

            float nodeWidth = guiWidth * 0.083f;

            RectF nodeRect = rtl ? RectF { searchRect.left - gaps - nodeWidth, searchRect.top, searchRect.left - gaps,
                                           searchRect.bottom }
                                 : RectF { searchRect.right + gaps, searchRect.top, searchRect.right + gaps + nodeWidth,
                                           searchRect.bottom };
            float pressDownHeight = searchRect.getHeight() / 10.f;

            for (auto& pair : modTabs) {
                RectF renderTabRect = nodeRect;

                float pressDownTranslate = pressDownHeight * std::get<3>(pair);
                renderTabRect = renderTabRect.translate(0.f, pressDownTranslate);

                bool contains = shouldSelect(renderTabRect, cursorPos);
                std::get<2>(pair) = util::LerpColorState(std::get<2>(pair), searchCol + 0.2f, searchCol, contains);

                if (justClicked[0] && contains) {
                    this->modTab = std::get<1>(pair);
                    playClickSound();
                    scroll = 0.f;
                    activeSetting = nullptr;
                    clearSettingBoxFocus();
                    plRenameBox.setSelected(false);
                    plRenamingTag.clear();
                    plPriorityDrag.clear();
                    kbRenameBox.setSelected(false);
                    kbRenamingBind.clear();
                    kbCapturingKey.clear();
                }

                std::get<3>(pair) = std::lerp(
                    std::get<3>(pair), ((contains && mouseButtons[0]) || modTab == std::get<1>(pair)) ? 1.f : 0.f,
                    Necromancer::getRenderer().getDeltaTime() / 5.f);

                contains = shouldSelect(renderTabRect, cursorPos);

                if (pressDownTranslate < 0.01f) dc.ctx->SetTarget(shadowBitmap.Get());
                D2D1_ROUNDED_RECT rr {};
                rr.radiusX = searchRound;
                rr.radiusY = searchRound;
                rr.rect = renderTabRect.get();
                auto solidBrush = rend.getSolidBrush();
                if (this->modTab == std::get<1>(pair)) {
                    solidBrush->SetColor((std::get<2>(pair) - 0.1f).get());
                } else {
                    solidBrush->SetColor(std::get<2>(pair).get());
                }
                dc.ctx->FillRoundedRectangle(rr, rend.getSolidBrush());

                float baseColor = 1.f - (0.1f * std::get<3>(pair));
                dc.drawSingleLineFitted(renderTabRect, LocalizeString::get(std::get<0>(pair)),
                                        { baseColor, baseColor, baseColor, 0.8f }, FontSelection::PrimaryRegular,
                                        nodeRect.getHeight() / 2.f, DWRITE_TEXT_ALIGNMENT_CENTER,
                                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                dc.ctx->SetTarget(myBitmap);

                auto oWidth = nodeRect.getWidth() + gaps;
                nodeRect.left += rtl ? -oWidth : oWidth;
                nodeRect.right += rtl ? -oWidth : oWidth;
            }
        }
    }

    // Panels
    if (this->tab == MODULES) {
        auto modulePad = guiWidth * 0.0317f;
        int numMods = 3;
        float modBetwPad = modulePad / 2.f;
        float totalPad = (modBetwPad * 2.f) + modulePad * 2.f;
        float modWidth = (guiWidth - totalPad) / numMods;
        float modHeight = 0.08F * rect.getHeight();
        float padFromSearchBar = 0.034F * rect.getHeight();

        float xStart = rtl ? rect.right - modulePad - modWidth : rect.left + modulePad;
        float x = xStart;
        float y = searchRect.bottom + padFromSearchBar;
        float modStartTop = y;

        if (modTab == CONFIG) {
            renderConfigTab(dc, { rect.left + modulePad, y, rect.right - modulePad, rect.bottom }, modulePad,
                            padFromSearchBar, rtl);
        } else if (modTab == PLAYERLIST) {
            renderPlayerListTab(dc, { rect.left + modulePad, y, rect.right - modulePad, rect.bottom }, modulePad, rtl);
        } else if (modTab == KEYBINDS) {
            renderKeybindsTab(dc, { rect.left + modulePad, y, rect.right - modulePad, rect.bottom }, modulePad, rtl);
        } else {
            dc.ctx->PushAxisAlignedClip({ rect.left, y, rect.right, rect.bottom }, D2D1_ANTIALIAS_MODE_ALIASED);
            modClip = { rect.left, y, rect.right, rect.bottom };

        y -= this->lerpScroll;

        this->scroll = std::clamp(scroll, 0.f, scrollMax);

        lerpScroll = std::lerp(lerpScroll, scroll, Necromancer::getRenderer().getDeltaTime() / 5.f);

        std::vector<std::reference_wrapper<ModuleLike>> displayedModLikes;

        // filter what mods get actually displayed (search box / selected category tab), put them in displayedModLikes

        const bool hasSearch = !searchTextBox.getText().empty();
        std::wstring lowerSearch = searchTextBox.getText();
        std::ranges::transform(lowerSearch, lowerSearch.begin(), tolower);

        for (auto& mod : mods) {
            if (!hasSearch) {
                if (!mod.mod || !isModuleInTab(*mod.mod)) continue;
            } else {
                std::wstring lower = mod.name;
                std::ranges::transform(lower, lower.begin(), tolower);

                if (lower.rfind(lowerSearch) == std::string::npos) continue;
            }

            displayedModLikes.emplace_back(mod);
        }

        std::ranges::sort(displayedModLikes, ModuleLike::isLess);

        if (hasSearch) {
            for (auto& modLikeRef : displayedModLikes) {
                auto& mod = modLikeRef.get();
                mod.shouldRender = false;
                std::wstring lower = mod.name;
                std::ranges::transform(lower, lower.begin(), tolower);

                if (lower.rfind(lowerSearch) != std::string::npos) mod.shouldRender = true;
            }
        }

        int i = 0;
        int row = 1;
        int column = 1;

        std::array<float, 3> columnOffs = { 0.f, 0.f, 0.f };

        // modules
        scrollMax = 0.f;

        for (auto& modLikeRef : displayedModLikes) {
            auto& mod = modLikeRef.get();

            if (!mod.shouldRender) continue;
            Vec2 pos = { x, y + columnOffs[i] };
            RectF modRect = { pos.x, pos.y, pos.x + modWidth, pos.y + modHeight };

            if (jumpModule.has_value() && mod.mod && mod.mod->name() == *jumpModule) {
                scroll = pos.y - modStartTop;
                mod.isExtended = true;
            }

            float maxHoverOffset = modRect.getHeight() / 10.f;
            modRect = modRect.translate(0.f, -(maxHoverOffset * mod.lerpHover));
            RectF modRectActual = modRect;

            if (mod.modRect.has_value()) {
                if (mod.modRect->bottom < rect.top || mod.modRect->top > rect.bottom) {
                    mod.modRect->setPos({ 0.f, pos.y });
                    goto end;
                }
            }

            {
                float textHeight = 0.4f * modHeight;
                float rlBounds = modWidth * 0.04561f;

                // toggle width/height

                float togglePad = modHeight * 0.249f;
                float toggleWidth = modWidth * 0.143f;

                RectF toggleRect = rtl ? RectF { modRect.left + togglePad, modRect.top + togglePad,
                                                 modRect.left + togglePad + toggleWidth, modRect.bottom - togglePad }
                                       : RectF { modRect.right - togglePad - toggleWidth, modRect.top + togglePad,
                                                 modRect.right - togglePad, modRect.bottom - togglePad };

                // module settings calculations
                dc.ctx->SetTarget(auxiliaryBitmap.Get());
                bool renderExtended = mod.mod && mod.lerpArrowRot < 0.995f;
                if (renderExtended) {
                    // clipped section
                    {
                        dc.ctx->Clear();

                        float textSizeDesc = textHeight * 0.72f;
                        float descTextPad = textSizeDesc / 3.f;
                        std::wstring resetText = LocalizeString::get("client.ui.clickGui.reset.name");
                        float resetTextSize = toggleRect.getHeight() * 0.55f;
                        float resetPadX = toggleRect.getHeight() * 0.4f;
                        float measuredResetWidth =
                            dc.getTextSize(resetText, Renderer::FontSelection::PrimaryRegular, resetTextSize, false,
                                           false, Vec2 { 10000.f, 10000.f })
                                .x +
                            resetPadX * 2.f;
                        float resetWidth =
                            std::min(std::max(measuredResetWidth, toggleRect.getWidth()), modWidth * 0.36f);
                        RectF resetRect =
                            rtl ? RectF { modRect.left + rlBounds, modRect.bottom, modRect.left + rlBounds + resetWidth,
                                          modRect.bottom + toggleRect.getHeight() }
                                : RectF { modRect.right - rlBounds - resetWidth, modRect.bottom,
                                          modRect.right - rlBounds, modRect.bottom + toggleRect.getHeight() };
                        float descResetGap = toggleRect.getHeight() * 0.45f;
                        RectF descTextRect =
                            rtl ? RectF { resetRect.right + descResetGap, modRect.bottom, modRect.right - rlBounds,
                                          modRect.bottom + textSizeDesc + descTextPad }
                                : RectF { modRect.left + rlBounds, modRect.bottom, resetRect.left - descResetGap,
                                          modRect.bottom + textSizeDesc + descTextPad };
                        descTextRect.bottom =
                            descTextRect.top +
                            dc.getMeasuredTextHeight(descTextRect, mod.description,
                                                     Renderer::FontSelection::PrimaryRegular, textSizeDesc, 4.f) +
                            descTextPad;
                        modRectActual.bottom = std::max(descTextRect.bottom, resetRect.bottom);

                        dc.drawWrappedTextClipped(descTextRect, mod.description, d2d::Color(1.f, 1.f, 1.f, 0.57f),
                                                  FontSelection::PrimaryRegular, textSizeDesc);

                        {
                            // Reset Button
                            dc.drawRoundedRectangle(resetRect, d2d::Color::RGB(0xFB, 0x36, 0x36),
                                                    resetRect.getHeight() * (0.223f), 0.5f,
                                                    DrawUtil::OutlinePosition::Inside);

                            dc.drawSingleLineFitted(resetRect, resetText, d2d::Color::RGB(0xFB, 0x36, 0x36),
                                                    Renderer::FontSelection::PrimaryRegular, resetTextSize,
                                                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                            if (resetRect.contains(cursorPos) && justClicked[0]) {
                                mod.mod->settings->forEach([&](std::shared_ptr<Setting> set) {
                                    *set->value = set->defaultValue;
                                    // std::visit([set](auto& obj) {
                                    //	static_assert(false, "");
                                    //	obj = std::get<std::remove_reference_t<decltype(obj)>>(set->defaultValue);
                                    //	}, *set->value);
                                    // });
                                });
                            }
                        }

                        float padToSetting = 0.014184f * rect.getHeight();

                        modRectActual.bottom += padToSetting;
                        mod.mod->settings->forEach([&](std::shared_ptr<Setting> set) {
                            if (set->visible) {
                                if (modRectActual.bottom <= rect.bottom) {
                                    if (set->shouldRender(*mod.mod->settings.get())) {
                                        float newY = drawSetting(set.get(), mod.mod->settings.get(),
                                                                 { descTextRect.left, modRectActual.bottom }, dc,
                                                                 descTextRect.getWidth(), 0.25f);
                                        modRectActual.bottom =
                                            (newY - modRectActual.bottom) > 2.f
                                                ? (newY + setting_height_relative * rect.getHeight() * 1.6f)
                                                : modRectActual.bottom;
                                    }
                                }
                            }
                        });

                        if (mod.mod->isHud() && static_cast<HUDModule*>(mod.mod.get())->isShowPreview()) {
                            auto rMod = static_cast<HUDModule*>(mod.mod.get());

                            RectF box = { modRectActual.left, modRectActual.bottom, modRectActual.right,
                                          modRectActual.bottom + mod.previewSize.y };

                            Vec2 drawPos = box.center(mod.previewSize);
                            D2D1::Matrix3x2F oTrans;

                            dc.ctx->GetTransform(&oTrans);
                            dc.ctx->SetTransform(D2D1::Matrix3x2F::Scale(1.f, 1.f) *
                                                 D2D1::Matrix3x2F::Translation(drawPos.x, drawPos.y));
                            rMod->render(dc, true, false);
                            mod.previewSize = rMod->getRectNonScaled().getSize();
                            dc.ctx->SetTransform(oTrans);
                            modRectActual.bottom += box.getHeight() * 1.25f;
                        }
                    }
                }
                dc.ctx->SetTarget(myBitmap);

                if (renderExtended) {
                    modRectActual.bottom =
                        (modRect.bottom + (modRectActual.getHeight() - modRect.getHeight()) * (1.f - mod.lerpArrowRot));
                    RectF clipRect = modRectActual;
                    clipRect.left -= 10.f;
                    clipRect.right += 10.f;
                    dc.ctx->PushAxisAlignedClip(clipRect.get(), D2D1_ANTIALIAS_MODE_ALIASED);
                }

                dc.fillRoundedRectangle(modRectActual, d2d::Color::RGB(0x44, 0x44, 0x44).asAlpha(0.22f),
                                        .22f * modHeight);
                dc.drawRoundedRectangle(modRectActual, accentColor.asAlpha(1.f * mod.lerpToggle), .22f * modHeight, 1.f,
                                        DrawUtil::OutlinePosition::Inside);
                if (renderExtended) {
                    dc.ctx->DrawBitmap(auxiliaryBitmap.Get());
                    dc.ctx->PopAxisAlignedClip();
                }

                // text
                auto textRect = modRect;
                if (rtl) {
                    textRect.right -= modRect.getWidth() / 6.f;
                } else {
                    textRect.left += modRect.getWidth() / 6.f;
                }

                float toggleNameMargin = modWidth * 0.018f;

                // Make the text end before the toggle rectangle
                if (!mod.mod || mod.mod->showToggle()) {
                    if (rtl) {
                        textRect.left = toggleRect.right + toggleNameMargin;
                    } else {
                        textRect.right = toggleRect.left - toggleNameMargin;
                    }
                }
                dc.drawWrappedTextClipped(textRect, mod.name, { 1.f, 1.f, 1.f, 1.f }, FontSelection::PrimaryLight,
                                          textHeight, DWRITE_TEXT_ALIGNMENT_LEADING,
                                          DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                // toggle

                if (mod.mod && mod.mod->showToggle()) {
                    Keybind* kbCardBind = nullptr;
                    KeybindEdit* kbCardEdit = nullptr;
                    if (!kbEditingBind.empty()) {
                        kbCardBind = KeybindManager::get().findBind(kbEditingBind);
                        if (kbCardBind) kbCardEdit = KeybindManager::findEdit(*kbCardBind, mod.mod->name(), "enabled");
                        if (kbCardBind && this->shouldSelect(toggleRect, cursorPos)) {
                            kbHoverLabel = mod.mod->name() + ".enabled";
                        }
                    }
                    if (mod.mod->shouldHoldToToggle()) {
                        d2d::Color color = d2d::Color::RGB(0xD9, 0xD9, 0xD9, 30);

                        dc.fillRoundedRectangle(toggleRect, color, toggleRect.getHeight() / 4.f);
                        dc.drawSingleLineFitted(toggleRect, util::StrToWStr(util::KeyToString(mod.mod->getKeybind())),
                                                { 1.f, 1.f, 1.f, 1.f }, Renderer::FontSelection::PrimaryRegular,
                                                toggleRect.getHeight() / 2.f, DWRITE_TEXT_ALIGNMENT_CENTER,
                                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                        if (this->shouldSelect(toggleRect, cursorPos)) {
                            setTooltip(LocalizeString::get("client.ui.clickGui.enableWithKeybind.desc"));
                            if (kbCardBind && justClicked[0]) {
                                bool cur = kbCardEdit && kbCardEdit->value.is_boolean()
                                    ? kbCardEdit->value.get<bool>()
                                    : mod.mod->isEnabled();
                                KeybindManager::get().recordEdit(kbEditingBind, mod.mod->name(), "enabled",
                                                                 (size_t)Setting::Type::Bool, !cur);
                                playClickSound();
                            }
                        }

                    } else if (!mod.mod->isToggleable()) {
                        bool selectAction = this->shouldSelect(toggleRect, cursorPos);
                        if (selectAction && justClicked[0]) {
                            if (kbCardBind) {
                                KeybindManager::get().recordEdit(kbEditingBind, mod.mod->name(), "enabled",
                                                                 (size_t)Setting::Type::Bool, true);
                            } else {
                                mod.mod->setEnabled(true);
                            }
                            playClickSound();
                        }

                        d2d::Color actionColor = selectAction ? accentColor : d2d::Color::RGB(0xD9, 0xD9, 0xD9, 30);
                        dc.fillRoundedRectangle(toggleRect, actionColor, toggleRect.getHeight() * 0.25f);
                        dc.drawSingleLineFitted(toggleRect, L"Open", { 1.f, 1.f, 1.f, 1.f },
                                                Renderer::FontSelection::PrimaryRegular, toggleRect.getHeight() * 0.45f,
                                                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    } else {
                        bool selecToggle = this->shouldSelect(toggleRect, cursorPos);
                        if (selecToggle) {
                            if (justClicked[0]) {
                                if (kbCardBind) {
                                    bool cur = kbCardEdit && kbCardEdit->value.is_boolean()
                                        ? kbCardEdit->value.get<bool>()
                                        : mod.mod->isEnabled();
                                    KeybindManager::get().recordEdit(kbEditingBind, mod.mod->name(), "enabled",
                                                                     (size_t)Setting::Type::Bool, !cur);
                                } else {
                                    mod.mod->setEnabled(!mod.mod->isEnabled());
                                }
                                playClickSound();
                            }
                        }
                        static auto offCol = d2d::Color(mod.toggleColorOff);

                        bool kbCardState = kbCardEdit && kbCardEdit->value.is_boolean()
                            ? kbCardEdit->value.get<bool>()
                            : mod.mod->isEnabled();

                        mod.toggleColorOn =
                            util::LerpColorState(mod.toggleColorOn, accentColor + 0.2f, accentColor, selecToggle);
                        mod.toggleColorOff =
                            util::LerpColorState(mod.toggleColorOff, offCol + 0.2f, offCol, selecToggle);

                        // float aTogglePadY = toggleRect.getHeight() * 0.15f;
                        float radius = toggleRect.getHeight() * 0.35f;
                        float circleOffs = toggleWidth * 0.27f;

                        dc.fillRoundedRectangle(toggleRect,
                                                kbCardState ? mod.toggleColorOn : mod.toggleColorOff,
                                                toggleRect.getHeight() / 2.f);
                        Vec2 center { rtl ? toggleRect.right - circleOffs : toggleRect.left + circleOffs,
                                      toggleRect.centerY() };
                        Vec2 center2 = center;
                        center2.x = rtl ? toggleRect.left + circleOffs : toggleRect.right - circleOffs;
                        float onDist = center2.x - center.x;

                        mod.lerpToggle = std::lerp(mod.lerpToggle, kbCardState ? 1.f : 0.f,
                                                   Necromancer::getRenderer().getDeltaTime() * 0.3f);

                        center.x += onDist * mod.lerpToggle;

                        dc.brush->SetColor(d2d::Color(0xB9, 0xB9, 0xB9).get());
                        dc.ctx->FillEllipse(D2D1::Ellipse({ center.x, center.y }, radius, radius), dc.brush);
                    }
                }

                RectF arrowRc = rtl ? RectF { modRect.right - modRect.getHeight() * 0.70f,
                                              modRect.top + (modRect.getHeight() * 0.4f),
                                              modRect.right - (modRect.getHeight() * 0.4f),
                                              modRect.bottom - modRect.getHeight() * 0.4f }
                                    : RectF { modRect.left + (modRect.getHeight() * 0.4f),
                                              modRect.top + (modRect.getHeight() * 0.4f),
                                              modRect.left + modRect.getHeight() * 0.70f,
                                              modRect.bottom - modRect.getHeight() * 0.4f };
                // arrow
                if (mod.mod) {
                    if (this->shouldSelect(modRect, cursorPos) && !shouldSelect(toggleRect, cursorPos)) {
                        if (justClicked[0] || justClicked[1]) {
                            mod.isExtended = !mod.isExtended;
                            activeSetting = nullptr;
                            clearSettingBoxFocus();
                        }
                    }

                    D2D1::Matrix3x2F oMatr;
                    dc.ctx->GetTransform(&oMatr);
                    float toLerp = mod.isExtended ? 0.f : 1.f;
                    dc.ctx->SetTransform(D2D1::Matrix3x2F::Rotation((1.f - mod.lerpArrowRot) * 180.f,
                                                                    { arrowRc.centerX(), arrowRc.centerY() }) *
                                         oMatr);
                    mod.lerpArrowRot = std::lerp(mod.lerpArrowRot, toLerp, Necromancer::getRenderer().getDeltaTime() * 0.3f);
                    // icon
                    dc.ctx->DrawBitmap(Necromancer::getAssets().arrowIcon.getBitmap(), arrowRc.get());
                    dc.ctx->SetTransform(oMatr);
                }
            }
        end:
            columnOffs[i] += modRectActual.getHeight() - modRect.getHeight();
            // set mod rect
            mod.modRect = modRectActual;

            mod.lerpHover = std::lerp(mod.lerpHover, shouldSelect(modRectActual, cursorPos) ? 1.f : 0.f,
                                      Necromancer::getRenderer().getDeltaTime() / 5.f);

            // scrolling max
            float scrollYNew = std::max(0.f, (modRectActual.bottom + padFromSearchBar) - rect.bottom) + lerpScroll;
            if (scrollYNew > scrollMax) scrollMax = scrollYNew;
            if (i >= (numMods - 1)) {
                i = 0;
                row++;
                column = 0;
                y += modRect.getHeight() + padFromSearchBar;
                x = xStart;
                continue;
            } else {
                x += (rtl ? -1.f : 1.f) * (modBetwPad + modWidth);
                column++;
            }
            i++;
        }

        dc.ctx->PopAxisAlignedClip();

        if (scrollMax > 0.f) {
            float trackWidth = 4.f * adaptedScale;
            d2d::Rect visibleListRect { rect.left, modStartTop, rect.right, rect.bottom };
            float trackX = rtl ? rect.left + (modulePad * 0.5f) - trackWidth : rect.right - (modulePad * 0.5f);
            d2d::Rect track { trackX, visibleListRect.top, trackX + trackWidth, visibleListRect.bottom };
            float thumbHeight = std::max(24.f * adaptedScale,
                                         visibleListRect.getHeight() *
                                             (visibleListRect.getHeight() / (visibleListRect.getHeight() + scrollMax)));
            float thumbY = track.top + ((track.getHeight() - thumbHeight) * (scroll / scrollMax));
            scrollbarTrackRect = track;
            scrollbarThumbRect = { track.left, thumbY, track.right, thumbY + thumbHeight };
            if (!colorPicker.setting && justClicked[0] && scrollbarTrackRect.contains(cursorPos)) {
                draggingScrollbar = true;
                if (scrollbarThumbRect.contains(cursorPos)) {
                    scrollbarDragOffset = cursorPos.y - scrollbarThumbRect.top;
                } else {
                    scrollbarDragOffset = scrollbarThumbRect.getHeight() * 0.5f;
                    updateScrollbarDrag(cursorPos);
                    thumbY = track.top + ((track.getHeight() - thumbHeight) * (scroll / scrollMax));
                    scrollbarThumbRect = { track.left, thumbY, track.right, thumbY + thumbHeight };
                }
            }
            dc.fillRoundedRectangle(scrollbarTrackRect, d2d::Color::RGB(0x55, 0x55, 0x55).asAlpha(0.28f),
                                    trackWidth * 0.5f);
            dc.fillRoundedRectangle(scrollbarThumbRect,
                                    (draggingScrollbar || scrollbarThumbRect.contains(cursorPos))
                                        ? accentColor
                                        : d2d::Color::RGB(0xD2, 0xD2, 0xD2).asAlpha(0.78f),
                                    trackWidth * 0.5f);
        } else {
            scrollbarTrackRect = {};
            scrollbarThumbRect = {};
        }
        }
    }

    dc.ctx->SetTransform(oTransform);
    dc.ctx->DrawImage(compositeEffect.Get());
    dc.ctx->SetTransform(currentMatr);

    modClip = std::nullopt;
    jumpModule = std::nullopt;

    renderKeybindEditorOverlay(dc, rtl);

    if (!kbParentPicker.bind.empty()) {
        drawParentPicker(dc);
        if (kbParentPicker.queueClose) {
            kbParentPicker = ParentPicker {};
            kbParentPickerRect = {};
        }
    }

    if (kbKindPicker.open) {
        drawKindPicker(dc);
        if (kbKindPicker.queueClose) {
            kbKindPicker = KindPicker {};
            kbKindPickerRect = {};
        }
    }

    if (blockPicker.mod) {
        drawBlockPicker(dc);
        if (blockPicker.queueClose) closeBlockPicker();
    }
    if (itemSwitcherPicker.mod) {
        drawItemSwitcher(dc);
        if (itemSwitcherPicker.queueClose) closeItemSwitcher();
    }

    if (colorPicker.setting) {
        drawColorPicker();
        if (colorPicker.queueClose) {
            auto& colVal = std::get<ColorValue>(*colorPicker.setting->value);
            colVal.isRGB = std::get<BoolValue>(colorPicker.rgbSelector);
            colVal.forceTagColor = std::get<BoolValue>(colorPicker.forceTagSelector);
            auto d2dCol = d2d::Color(util::HSVToColor(colorPicker.pickerColor)).asAlpha(colorPicker.opacityMod);
            *colorPicker.selectedColor = { d2dCol.r, d2dCol.g, d2dCol.b, d2dCol.a };
            colorPicker.setting->update();
            colorPicker.setting->userUpdate();
            colorPicker = ColorPicker();
        }
    }

    if (condCanvas.open) {
        justClicked[0] = condCanvasClick;
        drawCondCanvas(dc, rtl);
        if (condCanvas.queueClose) closeCondCanvas();
    }

    this->clearLayers();

    dc.ctx->SetTransform(oTransform);

    dc.ctx->SetTarget(Necromancer::getRenderer().getBitmap());
    // dc.ctx->DrawImage(myBitmap);

    if (shouldArrow) cursor = Cursor::Arrow;
}

void ClickGUI::onInit(Event&) {
    auto myBitmap = Necromancer::getRenderer().getBitmap();
    D2D1_SIZE_U bitmapSize = myBitmap->GetPixelSize();
    D2D1_PIXEL_FORMAT pixelFormat = myBitmap->GetPixelFormat();

    auto dc = Necromancer::getRenderer().getDeviceContext();

    dc->CreateBitmap(bitmapSize, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, pixelFormat),
                     shadowBitmap.GetAddressOf());
    dc->CreateBitmap(bitmapSize, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, pixelFormat),
                     auxiliaryBitmap.GetAddressOf());
    dc->CreateEffect(CLSID_D2D1Composite, compositeEffect.GetAddressOf());
}

void ClickGUI::onCleanup(Event&) {
    compositeEffect = nullptr;
    shadowBitmap = nullptr;
    auxiliaryBitmap = nullptr;
}

bool ClickGUI::hasSelectedSettingBox() const {
    for (auto const& item : settingBoxes) {
        if (item.second && item.second->isSelected()) return true;
    }
    return false;
}

void ClickGUI::clearSettingBoxFocus() {
    for (auto& item : settingBoxes) {
        if (item.second) item.second->setSelected(false);
    }
}

bool ClickGUI::hasActiveSetting() const {
    return activeSetting != nullptr || hasSelectedSettingBox();
}

void ClickGUI::close() {
    clearSettingBoxFocus();
    activeSetting = nullptr;
    dropdownSetting = nullptr;
    kbRenameBox.setSelected(false);
    kbRenamingBind.clear();
    kbCapturingKey.clear();
    kbEditingBind.clear();
    kbParentPicker = ParentPicker {};
    kbParentPickerRect = {};
    kbKindPicker = KindPicker {};
    kbKindPickerRect = {};
    closeCondCanvas();
    Screen::close();
}

void ClickGUI::onKey(Event& evGeneric) {
    auto& ev = reinterpret_cast<KeyUpdateEvent&>(evGeneric);
    if (ev.getKey() == VK_F11) return;

    const bool settingBoxSelected = hasSelectedSettingBox();
    if (!searchTextBox.isSelected() && !configNameTextBox.isSelected() && !plRenameBox.isSelected() &&
        !kbRenameBox.isSelected() && !blockSearchBox.isSelected() && !condItemSearchBox.isSelected() &&
        !condWaitMsBox.isSelected() && kbCapturingKey.empty() && !settingBoxSelected && !this->activeSetting &&
        Necromancer::getKeyboard().isMovementKey(ev.getKey())) {
        return;
    }

    if (condWaitMsBox.isSelected() || condThresholdBox.isSelected() || condCountBox.isSelected() ||
        condRangeBox.isSelected() || condDamageWindowBox.isSelected()) {
        if (ev.isDown() && (ev.getKey() == VK_RETURN || ev.getKey() == VK_ESCAPE)) {
            condWaitMsBox.setSelected(false);
            condThresholdBox.setSelected(false);
            condCountBox.setSelected(false);
            condRangeBox.setSelected(false);
            condDamageWindowBox.setSelected(false);
        }
        ev.setCancelled(true);
        return;
    }
    if (searchTextBox.isSelected() || configNameTextBox.isSelected() || plRenameBox.isSelected() ||
        kbRenameBox.isSelected() || settingBoxSelected) {
        ev.setCancelled(true);
        return;
    }
    if (condItemSearchBox.isSelected()) {
        if (ev.isDown() && ev.getKey() == VK_ESCAPE) {
            condItemSearchBox.setSelected(false);
            condCanvas.itemPickerNode = 0;
        }
        ev.setCancelled(true);
        return;
    }
    if (condCanvas.open && ev.isDown() && ev.getKey() == VK_ESCAPE) {
        if (condCanvas.itemPickerNode != 0) {
            condCanvas.itemPickerNode = 0;
            condItemSearchBox.setSelected(false);
        } else if (condCanvas.dragging || condCanvas.dragArmed) {
            condCanvas.dragging = false;
            condCanvas.dragArmed = false;
            condCanvas.dragNode = 0;
            condCanvas.dragNewKind = 0;
            condCanvas.dropValid = false;
            condCanvas.dropParent = 0;
            condCanvas.dropIndex = -1;
        } else {
            condCanvas.queueClose = true;
        }
        ev.setCancelled(true);
        return;
    }
    if (kbKindPicker.open && ev.isDown() && ev.getKey() == VK_ESCAPE) {
        kbKindPicker.queueClose = true;
        ev.setCancelled(true);
        return;
    }
    if (blockSearchBox.isSelected()) {
        if (ev.isDown() && ev.getKey() == VK_ESCAPE) {
            blockSearchBox.setSelected(false);
        }
        ev.setCancelled(true);
        return;
    }
    if (this->dropdownSetting && ev.isDown() && ev.getKey() == VK_ESCAPE) {
        dropdownSetting = nullptr;
        ev.setCancelled(true);
        return;
    }
    if (!kbParentPicker.bind.empty() && ev.isDown() && ev.getKey() == VK_ESCAPE) {
        kbParentPicker.queueClose = true;
        ev.setCancelled(true);
        return;
    }
    if (!kbCapturingKey.empty()) {
        if (ev.getKey() >= 0x01 && ev.getKey() <= 0x06) return;
        if (ev.isDown()) {
            if (ev.getKey() != VK_ESCAPE) {
                KeybindManager::get().setKey(kbCapturingKey, ev.getKey());
            }
            kbCapturingKey.clear();
            playClickSound();
        }
        ev.setCancelled(true);
        return;
    }
    if (this->activeSetting) {
        if (ev.getKey() >= 0x01 && ev.getKey() <= 0x06) return;
        if (ev.isDown()) {
            if (ev.getKey() == VK_ESCAPE) {
                activeSetting = nullptr;
                ev.setCancelled(true);
                return;
            } else {
                this->capturedKey = ev.getKey();
            }
        }
    }

    if (ev.isDown() && ev.getKey() == VK_ESCAPE && !kbEditingBind.empty()) {
        kbEditingBind.clear();
        ev.setCancelled(true);
        return;
    }

    if (ev.isDown() && ev.getKey() == VK_ESCAPE) {
        if (colorPicker.setting) {
            colorPicker.queueClose = true;
        } else if (blockPicker.mod) {
            if (blockPicker.addView) {
                blockPicker.addView = false;
                blockPicker.scroll = 0.f;
                blockPicker.lerpScroll = 0.f;
                blockSearchBox.setSelected(false);
            } else {
                blockPicker.queueClose = true;
            }
        } else if (itemSwitcherPicker.mod) {
            itemSwitcherPicker.queueClose = true;
        } else {
            this->close();
        }
    }

    ev.setCancelled(true);
}

void ClickGUI::onClick(Event& evGeneric) {
    auto& ev = reinterpret_cast<ClickEvent&>(evGeneric);
    if (ev.isDown()) {
        if (const int vk = mouseButtonToVk(ev.getMouseButton())) {
            if (!kbCapturingKey.empty()) {
                KeybindManager::get().setKey(kbCapturingKey, vk);
                kbCapturingKey.clear();
                playClickSound();
                ev.setCancelled(true);
                return;
            }
            if (this->activeSetting) {
                this->capturedKey = vk;
                ev.setCancelled(true);
                return;
            }
        }
    }
    if (ev.getMouseButton() >= 4) {
        ev.setCancelled(true);
    }

    if (ev.getClickType() == ClickEvent::ClickType::Wheel) {
        auto mouse = SDK::ClientInstance::get()->cursorPos;
        float delta = static_cast<float>(ev.getWheelDelta()) / 3.f;

        if (condCanvas.open) {
            if (condCanvas.itemPickerNode != 0 && condItemPickerRect.contains(mouse)) {
                condCanvas.itemScroll =
                    std::clamp(condCanvas.itemScroll - delta, 0.f, condCanvas.itemScrollMax);
            } else if (condPaletteRect.contains(mouse)) {
                condCanvas.paletteScroll =
                    std::clamp(condCanvas.paletteScroll - delta, 0.f, condCanvas.paletteScrollMax);
            } else if (condBoardRect.contains(mouse)) {
                condCanvas.boardScroll =
                    std::clamp(condCanvas.boardScroll - delta * 4.f, 0.f, condCanvas.boardScrollMax);
            }
        } else if (!kbParentPicker.bind.empty() && kbParentPickerRect.contains(mouse)) {
            kbParentPicker.scroll =
                std::clamp(kbParentPicker.scroll - delta, 0.f, kbParentPicker.scrollMax);
        } else if (blockPicker.mod && bPickerRect.contains(mouse)) {
            blockPicker.scroll = std::clamp(blockPicker.scroll - delta, 0.f, blockPicker.scrollMax);
        } else if (itemSwitcherPicker.mod) {
            itemSwitcherPicker.scroll = std::clamp(itemSwitcherPicker.scroll - delta, 0.f, itemSwitcherPicker.scrollMax);
        } else {
            this->scroll = std::clamp(scroll - delta, 0.f, scrollMax);
        }
        ev.setCancelled(true);
        return;
    }

    if (ev.getClickType() != ClickEvent::ClickType::Left) return;
}

namespace {
    void drawAlphaBar(D2DUtil& dc, d2d::Rect rect, float nodeSize, int rows) {
        float endY = rect.top;
        endY += rect.getHeight() / rows;
        float beginY = rect.top;
        // gray part
        float bs = nodeSize;

        for (int i = 0; i < rows; i++) {
            if (i % 2 == 0) {
                for (float beginX = rect.left; beginX < rect.right; beginX += bs * 2.f) {
                    float endX = std::min(rect.right, beginX + bs);
                    dc.fillRectangle({ beginX, beginY, endX, endY }, { 1.f, 1.f, 1.f, 0.5f });
                }
            } else {
                for (float beginX = rect.left + bs; beginX < rect.right; beginX += bs * 2.f) {
                    float endX = std::min(rect.right, beginX + bs);
                    dc.fillRectangle({ beginX, beginY, endX, endY }, { 1.f, 1.f, 1.f, 0.5f });
                }
            }
            beginY = endY;
            endY += rect.getHeight() / rows;
        }
    }
}

float ClickGUI::drawSetting(Setting* set, SettingGroup* group, Vec2 const& pos, D2DUtil& dc, float size, float fTextWidth,
                            bool bypassClickThrough) {
    const float checkboxSize = rect.getWidth() * setting_height_relative;
    const float textSize = checkboxSize * 0.8f;
    const auto cursorPos = SDK::ClientInstance::get()->cursorPos;
    const float round = 0.1875f * checkboxSize;
    const bool rtl = Necromancer::get().getL10nData().isSelectedLanguageRightToLeft();

    auto accentColor = d2d::Color(Necromancer::get().getAccentColor().getMainColor());

    Keybind* kbBind = nullptr;
    KeybindEdit* kbEdit = nullptr;
    if (!kbEditingBind.empty() && group) {
        kbBind = KeybindManager::get().findBind(kbEditingBind);
        if (kbBind) kbEdit = KeybindManager::findEdit(*kbBind, group->name(), set->name());
    }

    switch (static_cast<Setting::Type>(set->value->index())) {
    case Setting::Type::Text: {
        RectF rc = { pos.x, pos.y, (pos.x + size) - (fTextWidth * size), pos.y + checkboxSize };
        RectF txtRc = rc;
        RectF rightRc = rc;

        float labelWidth = rc.getWidth() * (fTextWidth * 1.5f);
        if (rtl) {
            txtRc.right -= labelWidth;
            rightRc.left = txtRc.right;
        } else {
            txtRc.left += labelWidth;
            rightRc.right = txtRc.left;
        }

        d2d::Color col = d2d::Color::RGB(0x8D, 0x8D, 0x8D).asAlpha(0.11f);
        std::shared_ptr<TextBox> tb;
        for (auto& items : settingBoxes) {
            if (items.first == set) {
                tb = items.second;
            }
        }

        auto& textVal = std::get<TextValue>(*set->value);
        if (!tb) {
            tb = std::make_shared<TextBox>(txtRc);
            tb->setText(textVal.str);
            tb->setCaretLocation(static_cast<int>(textVal.str.size()));
            settingBoxes[set] = tb;
            Necromancer::get().addTextBox(settingBoxes[set].get());
        }
        tb->setRect(txtRc);
        tb->render(dc, round, col, D2D1::ColorF::White);
        if (tb->isSelected()) {
            dc.drawRoundedRectangle(txtRc, D2D1::ColorF::White, round, 1.f);
        }

        if (justClicked[0]) {
            if (shouldSelect(tb->getRect(), cursorPos))
                tb->setSelected(true);
            else
                tb->setSelected(false);
        }

        textVal.str = tb->getText();
        if (kbBind && shouldSelect(tb->getRect(), cursorPos)) {
            setTooltip(LocalizeString::get("client.ui.clickGui.keybinds.notBindable.desc"));
        }
        auto label = set->getDisplayName();
        rightRc.bottom = rightRc.top + dc.getMeasuredTextHeight(
                                           rightRc, label, Renderer::FontSelection::PrimarySemilight, textSize, 3.f);
        dc.drawWrappedTextClipped(rightRc, label, { 1.f, 1.f, 1.f, 1.f }, Renderer::FontSelection::PrimarySemilight,
                                  textSize);
        return std::max(txtRc.bottom, rightRc.bottom);
    } break;
    case Setting::Type::Bool: {
        RectF checkboxRect = d2d::controlAtStart(pos, size, checkboxSize, checkboxSize, rtl);
        float offs = checkboxSize * 0.66f;
        RectF textRect = d2d::labelAfterStartControl(checkboxRect, pos, size, offs, rtl);
        auto disp = set->getDisplayName();
        float labelHeight = dc.getMeasuredTextHeight(textRect, disp, FontSelection::PrimarySemilight, textSize, 3.f);
        float rowHeight = std::max(checkboxSize, labelHeight);
        checkboxRect = checkboxRect.translate(0.f, (rowHeight - checkboxSize) * 0.5f);
        textRect = d2d::labelAfterStartControl(checkboxRect, pos, size, offs, rtl);
        textRect.top = pos.y;
        textRect.bottom = pos.y + rowHeight;

        bool contains =
            bypassClickThrough ? checkboxRect.contains(cursorPos) : this->shouldSelect(checkboxRect, cursorPos);
        if (kbBind && contains) kbHoverLabel = group->name() + "." + set->name();

        auto colOff = d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.11f);
        if (!set->rendererInfo.init) {
            set->rendererInfo.init = true;
            set->rendererInfo.col[0] = colOff.r;
            set->rendererInfo.col[1] = colOff.g;
            set->rendererInfo.col[2] = colOff.b;
            set->rendererInfo.col[3] = colOff.a;
        }
        auto lerpedColor = util::LerpColorState(set->rendererInfo.col, colOff + 0.1f, colOff, contains);
        set->rendererInfo.col[0] = lerpedColor.r;
        set->rendererInfo.col[1] = lerpedColor.g;
        set->rendererInfo.col[2] = lerpedColor.b;
        set->rendererInfo.col[3] = lerpedColor.a;

        if (contains && justClicked[0]) {
            if (kbBind) {
                bool cur = kbEdit && kbEdit->value.is_boolean() ? kbEdit->value.get<bool>()
                                                                : std::get<BoolValue>(*set->value).value;
                KeybindManager::get().recordEdit(kbEditingBind, group->name(), set->name(),
                                                 (size_t)Setting::Type::Bool, !cur);
            } else {
                std::get<BoolValue>(*set->value) = !std::get<BoolValue>(*set->value);
                set->update();
                set->userUpdate();
            }
            playClickSound();
        }

        bool kbBoolState = kbEdit && kbEdit->value.is_boolean() ? kbEdit->value.get<bool>()
                                                                : std::get<BoolValue>(*set->value).value;
        dc.fillRoundedRectangle(checkboxRect, Color(set->rendererInfo.col), round);
        if (kbBoolState) {
            float checkWidth = 0.6f * checkboxSize;
            float checkHeight = 0.375f * checkboxSize;
            RectF markRect = { checkboxRect.left + checkWidth / 4.f, checkboxRect.top + checkHeight / 2.f,
                               checkboxRect.right - checkWidth / 4.f, checkboxRect.bottom - checkHeight / 2.f };

            dc.ctx->DrawBitmap(Necromancer::getAssets().checkmarkIcon.getBitmap(), markRect);
        }

        dc.drawWrappedTextClipped(textRect, disp, { 1.f, 1.f, 1.f, 1.f }, FontSelection::PrimarySemilight, textSize);
        auto desc = set->desc();
        if (!desc.empty())
            if (contains || shouldSelect(textRect, cursorPos)) setTooltip(desc);
        return pos.y + rowHeight;
    } break;
    case Setting::Type::Key: {
        RectF keyRect = d2d::controlAtStart(pos, size, checkboxSize * 2.f, checkboxSize, rtl);
        std::wstring text = util::StrToWStr(util::KeyToString(std::get<KeyValue>(*set->value)));
        float keyTextSize = textSize * 0.9f;
        auto ts =
            dc.getTextSize(text, FontSelection::PrimaryRegular, keyTextSize, false, false, Vec2 { 10000.f, 10000.f }) +
            Vec2(8.f, 0.f);
        float maxKeyWidth = std::max(checkboxSize * 2.f, size * 0.38f);
        float keyWidth = std::clamp(ts.x, checkboxSize * 2.f, maxKeyWidth);
        if (rtl)
            keyRect.left = keyRect.right - keyWidth;
        else
            keyRect.right = keyRect.left + keyWidth;

        float padToName = 0.006335f * rect.getWidth();
        RectF textRect = d2d::labelAfterStartControl(keyRect, pos, size, padToName, rtl);
        auto disp = set->getDisplayName();
        float labelHeight = dc.getMeasuredTextHeight(textRect, disp, FontSelection::PrimarySemilight, textSize, 3.f);
        float rowHeight = std::max(checkboxSize, labelHeight);
        keyRect = keyRect.translate(0.f, (rowHeight - checkboxSize) * 0.5f);
        textRect = d2d::labelAfterStartControl(keyRect, pos, size, padToName, rtl);
        textRect.top = pos.y;
        textRect.bottom = pos.y + rowHeight;

        bool contains = this->shouldSelect(keyRect, cursorPos);

        auto colOff = d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.11f);
        if (!set->rendererInfo.init) {
            set->rendererInfo.init = true;
            set->rendererInfo.col[0] = colOff.r;
            set->rendererInfo.col[1] = colOff.g;
            set->rendererInfo.col[2] = colOff.b;
            set->rendererInfo.col[3] = colOff.a;
        }
        auto lerpedColor = util::LerpColorState(set->rendererInfo.col, colOff + 0.1f, colOff, contains);
        set->rendererInfo.col[0] = lerpedColor.r;
        set->rendererInfo.col[1] = lerpedColor.g;
        set->rendererInfo.col[2] = lerpedColor.b;
        set->rendererInfo.col[3] = lerpedColor.a;

        if (set == activeSetting) {
            if (justClicked[0] && !contains) {
                activeSetting = nullptr;
            }
        }

        // white outline
        if (set == activeSetting) {
            text = L"...";
        }

        dc.fillRoundedRectangle(keyRect, Color(set->rendererInfo.col), round);

        dc.drawSingleLineFitted(keyRect, text, d2d::Color(1.f, 1.f, 1.f, 1.f), FontSelection::PrimaryRegular,
                                keyTextSize, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        if (activeSetting == set) {
            dc.drawRoundedRectangle(keyRect, d2d::Color(1.f, 1.f, 1.f, 1.f), round);
        }
        if (activeSetting == set && this->capturedKey > 0) {
            std::get<KeyValue>(*set->value) = capturedKey;
            set->update();
            set->userUpdate();
            activeSetting = 0;
            capturedKey = 0;
        }

        dc.drawWrappedTextClipped(textRect, disp, { 1.f, 1.f, 1.f, 1.f }, FontSelection::PrimarySemilight, textSize);
        if (!set->desc().empty())
            if (shouldSelect(textRect, cursorPos)) setTooltip(set->desc());
        if (shouldSelect(keyRect, cursorPos)) {
            if (kbBind) {
                setTooltip(LocalizeString::get("client.ui.clickGui.keybinds.notBindable.desc"));
            } else {
                setTooltip(LocalizeString::get("client.ui.clickGui.rightClickReset.desc"));
                if (justClicked[0]) {
                    if (!this->activeSetting) activeSetting = set;
                    playClickSound();
                }
                if (justClicked[1]) {
                    activeSetting = nullptr;
                    std::get<KeyValue>(*set->value) = 0;
                    set->update();
                    set->userUpdate();
                    playClickSound();
                }
            }
        }
        return pos.y + rowHeight;
    }
    case Setting::Type::Enum: {
        RectF enumRect = d2d::controlAtStart(pos, size, checkboxSize * 2.f, checkboxSize, rtl);

        EnumValue& val = std::get<EnumValue>(*set->value);
        auto* entries = set->enumData->getEntries();
        if (!entries || entries->empty()) {
            return enumRect.bottom;
        }

        val.val = std::clamp(val.val, 0, static_cast<int>(entries->size()) - 1);

        auto colOff = d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.11f);
        if (!set->rendererInfo.init) {
            set->rendererInfo.init = true;
            set->rendererInfo.col[0] = colOff.r;
            set->rendererInfo.col[1] = colOff.g;
            set->rendererInfo.col[2] = colOff.b;
            set->rendererInfo.col[3] = colOff.a;
        }
        auto text = set->enumData->getSelectedName();
        int kbEnumSel = val.val;
        if (kbEdit && kbEdit->value.is_number()) {
            kbEnumSel = std::clamp(kbEdit->value.get<int>(), 0, static_cast<int>(entries->size()) - 1);
            text = entries->at(kbEnumSel).name();
        }

        float entryPadX = checkboxSize * 0.42f;
        float arrowSize = checkboxSize * 0.35f;
        float arrowPad = checkboxSize * 0.45f;
        float enumTextSize = textSize * 0.9f;
        auto ts = dc.getTextSize(text, FontSelection::PrimarySemilight, enumTextSize, false, false,
                                 Vec2 { 10000.f, 10000.f }) +
                  Vec2(8.f, 0.f);
        float maxEntryTextWidth = ts.x;
        for (auto& entry : *entries) {
            maxEntryTextWidth =
                std::max(maxEntryTextWidth, dc.getTextSize(entry.name(), FontSelection::PrimaryRegular, enumTextSize,
                                                           false, false, Vec2 { 10000.f, 10000.f })
                                                .x);
        }

        float maxDropdownWidth = std::max(checkboxSize * 2.f, size * 0.45f);
        float dropdownWidth =
            std::min(maxDropdownWidth,
                     std::max(enumRect.getWidth(), maxEntryTextWidth + entryPadX * 2.f + arrowSize + arrowPad));
        if (rtl)
            enumRect.left = enumRect.right - dropdownWidth;
        else
            enumRect.right = enumRect.left + dropdownWidth;

        float padToName = 0.006335f * rect.getWidth();
        RectF textRect = d2d::labelAfterStartControl(enumRect, pos, size, padToName, rtl);
        auto label = set->getDisplayName();
        float labelHeight = dc.getMeasuredTextHeight(textRect, label, FontSelection::PrimaryRegular, textSize, 3.f);
        float rowHeight = std::max(checkboxSize, labelHeight);
        enumRect = enumRect.translate(0.f, (rowHeight - checkboxSize) * 0.5f);
        textRect = d2d::labelAfterStartControl(enumRect, pos, size, padToName, rtl);
        textRect.top = pos.y;
        textRect.bottom = pos.y + rowHeight;
        float settingBottom = pos.y + rowHeight;

        bool contains = this->shouldSelect(enumRect, cursorPos);
        bool dropdownOpen = dropdownSetting == set;
        float& dropdownAnim = dropdownAnimations[set];
        dropdownAnim = std::lerp(dropdownAnim, dropdownOpen ? 1.f : 0.f, Necromancer::getRenderer().getDeltaTime() * 0.3f);
        if (dropdownOpen && dropdownAnim > 0.995f)
            dropdownAnim = 1.f;
        else if (!dropdownOpen && dropdownAnim < 0.005f)
            dropdownAnim = 0.f;
        bool renderDropdown = dropdownAnim > 0.f;

        auto lerpedColor = util::LerpColorState(set->rendererInfo.col, colOff + 0.1f, colOff, contains);
        set->rendererInfo.col[0] = lerpedColor.r;
        set->rendererInfo.col[1] = lerpedColor.g;
        set->rendererInfo.col[2] = lerpedColor.b;
        set->rendererInfo.col[3] = lerpedColor.a;

        if (kbBind && contains) kbHoverLabel = group->name() + "." + set->name();
        dc.fillRoundedRectangle(enumRect, Color(set->rendererInfo.col), round);

        RectF arrowRect = rtl ? RectF { enumRect.left + arrowPad, enumRect.centerY(arrowSize),
                                        enumRect.left + arrowPad + arrowSize, enumRect.centerY(arrowSize) + arrowSize }
                              : RectF { enumRect.right - arrowPad - arrowSize, enumRect.centerY(arrowSize),
                                        enumRect.right - arrowPad, enumRect.centerY(arrowSize) + arrowSize };
        RectF selectedTextRect = rtl ? RectF { enumRect.left + arrowPad + arrowSize + entryPadX, enumRect.top,
                                               enumRect.right - entryPadX, enumRect.bottom }
                                     : RectF { enumRect.left + entryPadX, enumRect.top,
                                               enumRect.right - arrowPad - arrowSize - entryPadX, enumRect.bottom };
        dc.drawSingleLineFitted(selectedTextRect, text, d2d::Color(1.f, 1.f, 1.f, 1.f), FontSelection::PrimaryRegular,
                                enumTextSize, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        dc.drawBitmapRotated(Necromancer::getAssets().arrowIcon.getBitmap(), arrowRect, dropdownAnim * 180.f, 0.92f);

        if (renderDropdown) {
            dc.drawRoundedRectangle(enumRect, d2d::Color(1.f, 1.f, 1.f, 1.f), round);
        }

        dc.drawWrappedTextClipped(textRect, label, { 1.f, 1.f, 1.f, 1.f }, FontSelection::PrimaryRegular, textSize);
        if (!set->desc().empty())
            if (shouldSelect(textRect, cursorPos)) setTooltip(set->desc());

        if (contains) {
            if (set->enumData->getSelectedDesc().size() > 0) {
                setTooltip(set->enumData->getSelectedDesc());
            } else
                setTooltip(set->enumData->getSelectedName());
        }

        float dropdownBottom = enumRect.bottom;
        if (renderDropdown) {
            float entryHeight = checkboxSize * 1.16f;
            float dropdownPad = checkboxSize * 0.22f;
            RectF dropdownRect = { enumRect.left, enumRect.bottom + dropdownPad, enumRect.right,
                                   enumRect.bottom + dropdownPad + entryHeight * static_cast<float>(entries->size()) };
            RectF animatedDropdownRect = dropdownRect;
            animatedDropdownRect.bottom = dropdownRect.top + dropdownRect.getHeight() * dropdownAnim;

            dc.ctx->PushAxisAlignedClip(animatedDropdownRect.get(), D2D1_ANTIALIAS_MODE_ALIASED);
            dc.fillRoundedRectangle(dropdownRect, d2d::Color::RGB(0x2E, 0x2E, 0x2E).asAlpha(0.96f), round);
            dc.drawRoundedRectangle(dropdownRect, d2d::Color(1.f, 1.f, 1.f, 0.18f), round, 0.75f,
                                    DrawUtil::OutlinePosition::Inside);

            bool clickedEntry = false;
            for (int i = 0; i < static_cast<int>(entries->size()); ++i) {
                RectF entryRect = { dropdownRect.left, dropdownRect.top + entryHeight * static_cast<float>(i),
                                    dropdownRect.right, dropdownRect.top + entryHeight * static_cast<float>(i + 1) };
                bool entryHovered = dropdownOpen && animatedDropdownRect.contains(cursorPos) &&
                                    this->shouldSelect(entryRect, cursorPos);
                bool entrySelected = i == kbEnumSel;
                if (entryHovered || entrySelected) {
                    dc.fillRoundedRectangle(entryRect,
                                            (entrySelected ? accentColor : d2d::Color::RGB(0xD9, 0xD9, 0xD9))
                                                .asAlpha(entrySelected ? 0.34f : 0.12f),
                                            round);
                }

                RectF entryTextRect = { entryRect.left + entryPadX, entryRect.top, entryRect.right - entryPadX,
                                        entryRect.bottom };
                dc.drawSingleLineFitted(entryTextRect, entries->at(i).name(), d2d::Colors::WHITE,
                                        FontSelection::PrimaryRegular, enumTextSize, DWRITE_TEXT_ALIGNMENT_LEADING,
                                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                if (entryHovered) {
                    auto desc = entries->at(i).desc();
                    setTooltip(desc.empty() ? entries->at(i).name() : desc);
                    if (justClicked[0]) {
                        if (kbBind) {
                            if (kbEnumSel != i) {
                                KeybindManager::get().recordEdit(kbEditingBind, group->name(), set->name(),
                                                                 (size_t)Setting::Type::Enum, i);
                            }
                        } else if (val.val != i) {
                            val.val = i;
                            set->update();
                            set->userUpdate();
                        }

                        dropdownSetting = nullptr;
                        clickedEntry = true;
                        playClickSound();
                    }
                }
            }
            dc.ctx->PopAxisAlignedClip();

            if (dropdownOpen && justClicked[0] && !contains && !this->shouldSelect(dropdownRect, cursorPos) &&
                !clickedEntry) {
                dropdownSetting = nullptr;
            }

            dropdownBottom = animatedDropdownRect.bottom;
        }

        if (contains && justClicked[0]) {
            dropdownSetting = dropdownOpen ? nullptr : set;
            activeSetting = nullptr;
            playClickSound();
        }

        return renderDropdown ? std::max(dropdownBottom, settingBottom) : settingBottom;
    }
    case Setting::Type::Color: {
        float padToName = 0.006335f * rect.getWidth();

        RectF colRect = d2d::controlAtStart(pos, size, checkboxSize * 2.f, checkboxSize, rtl);
        bool contains = this->shouldSelect(colRect, cursorPos);
        std::wstring name = set->getDisplayName();

        auto& colVal = std::get<ColorValue>(*set->value);

        RectF textRect = d2d::labelAfterStartControl(colRect, pos, size, padToName, rtl);
        float labelHeight = dc.getMeasuredTextHeight(textRect, name, FontSelection::PrimarySemilight, textSize, 3.f);
        float rowHeight = std::max(checkboxSize, labelHeight);
        colRect = colRect.translate(0.f, (rowHeight - checkboxSize) * 0.5f);
        textRect = d2d::labelAfterStartControl(colRect, pos, size, padToName, rtl);
        textRect.top = pos.y;
        textRect.bottom = pos.y + rowHeight;
        contains = this->shouldSelect(colRect, cursorPos);
        dc.drawWrappedTextClipped(textRect, name, { 1.f, 1.f, 1.f, 1.f }, FontSelection::PrimarySemilight, textSize);

        ComPtr<ID2D1LinearGradientBrush> gradientBrush;
        ComPtr<ID2D1GradientStopCollection> gradientStopCollection;

        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES prop {};
        auto ss = Necromancer::getRenderer().getScreenSize();
        prop.startPoint = { 0.f, ss.height / 2.f };
        prop.endPoint = { ss.width, ss.height / 2.f };

        d2d::Color col = { colVal.getMainColor().r, colVal.getMainColor().g, colVal.getMainColor().b,
                           colVal.getMainColor().a };

        const D2D1_GRADIENT_STOP stops[] = { 0.f, col.asAlpha(1.f).get(), 1.f, col.get() };

        dc.ctx->CreateGradientStopCollection(stops, _countof(stops), gradientStopCollection.GetAddressOf());
        dc.ctx->CreateLinearGradientBrush(prop, gradientStopCollection.Get(), gradientBrush.GetAddressOf());

        gradientBrush->SetStartPoint({ colRect.left, colRect.centerY() });
        gradientBrush->SetEndPoint({ colRect.right, colRect.centerY() });
        ComPtr<ID2D1GradientStopCollection> stopCol;

        dc.fillRoundedRectangle(colRect, { 1.f, 1.f, 1.f, 0.4f }, round);
        // alpha bar

        float apad = 1.f;
        dc.ctx->PushAxisAlignedClip(
            { colRect.left + apad, colRect.top + apad, colRect.right - apad, colRect.bottom - apad },
            D2D1_ANTIALIAS_MODE_ALIASED);
        drawAlphaBar(dc, colRect, colRect.getWidth() / 8.f, 6);
        dc.ctx->PopAxisAlignedClip();

        dc.fillRoundedRectangle(colRect, gradientBrush.Get(), round);
        dc.drawRoundedRectangle(colRect, gradientBrush.Get(), round, 1.f, DrawUtil::OutlinePosition::Inside);

        if (bypassClickThrough ? colRect.contains(cursorPos) : shouldSelect(colRect, cursorPos)) {
            if (justClicked[0]) {
                playClickSound();
                colorPicker.setting = set;
                std::get<BoolValue>(colorPicker.rgbSelector) = std::get<ColorValue>(*set->value).isRGB;
                std::get<BoolValue>(colorPicker.forceTagSelector) =
                    std::get<ColorValue>(*set->value).forceTagColor;
                colorPicker.dragging = false;
                float pickerWidth = 0.2419f * rect.getWidth();
                cPickerRect = rtl ? RectF { colRect.right - pickerWidth, colRect.bottom + 20.f, 0.f, 0.f }
                                  : RectF { colRect.left, colRect.bottom + 20.f, 0.f, 0.f };
                colorPicker.selectedColor = &colVal.color1;
                auto sCol = *colorPicker.selectedColor;
                colorPicker.pickerColor = util::ColorToHSV({ sCol.r, sCol.g, sCol.b, sCol.a });
                colorPicker.hueMod = colorPicker.pickerColor.h / 360.f;
                colorPicker.svModX = colorPicker.pickerColor.s;
                colorPicker.svModY = 1.f - colorPicker.pickerColor.v;
                colorPicker.opacityMod = sCol.a;
            }
        }
        if (kbBind && shouldSelect(colRect, cursorPos)) {
            setTooltip(LocalizeString::get("client.ui.clickGui.keybinds.notBindable.desc"));
        }
        return pos.y + rowHeight;
    } break;
    case Setting::Type::Float: {
        float sliderHeight = (rect.getHeight() * 0.017730f);
        float textSz = textSize;
        std::wstringstream namew;
        namew << set->getDisplayName();

        float min = std::get<FloatValue>(set->min);
        float max = std::get<FloatValue>(set->max);
        float interval = std::get<FloatValue>(set->interval);

        bool kbHasFloat = kbEdit && kbEdit->value.is_number();
        float dispVal = kbHasFloat ? kbEdit->value.get<float>() : std::get<FloatValue>(*set->value).value;

        std::wstringstream valuew;
        valuew << dispVal;
        std::wstring valueText = valuew.str();

        auto& valueBox = settingBoxes[set];
        if (!valueBox) {
            valueBox = std::make_shared<TextBox>(RectF {}, 16, true);
            valueBox->setText(valueText);
            valueBox->setCaretLocation(static_cast<int>(valueText.size()));
            Necromancer::get().addTextBox(valueBox.get());
        }
        if (!valueBox->isSelected()) {
            valueBox->setText(valueText);
            valueBox->setCaretLocation(static_cast<int>(valueText.size()));
        }
        std::wstring measureText =
            valueBox->isSelected() && !valueBox->getText().empty() ? valueBox->getText() : valueText;

        RectF textRect = { pos.x, pos.y, pos.x + size, pos.y + checkboxSize };
        textRect.bottom = textRect.top +
                          dc.getMeasuredTextHeight(textRect, namew.str(), FontSelection::PrimarySemilight, textSz, 3.f);
        dc.drawWrappedTextClipped(textRect, namew.str(), d2d::Color(1.f, 1.f, 1.f, 1.f),
                                  FontSelection::PrimarySemilight, textSz);

        float sliderPadTop = checkboxSize * 0.35f;
        float sliderTop = textRect.bottom + sliderPadTop;
        float valueTextSize = sliderHeight * 1.4f;
        float valuePadX = checkboxSize * 0.35f;
        float measuredValueWidth = dc.getTextSize(measureText, Renderer::FontSelection::PrimarySemilight,
                                                  valueTextSize, false, false, Vec2 { 10000.f, 10000.f })
                                       .x +
                                   valuePadX * 2.f;
        float valueWidth = std::min(std::max(measuredValueWidth, checkboxSize * 2.f), size * 0.35f);
        float gapToValue = checkboxSize * 0.35f;
        float valueTextHeight =
            std::max(sliderHeight, dc.getTextLineHeight(Renderer::FontSelection::PrimarySemilight, valueTextSize));
        float valueTextTop = sliderTop + (sliderHeight - valueTextHeight) * 0.5f;
        RectF rightRect =
            rtl ? RectF { pos.x, valueTextTop, pos.x + valueWidth, valueTextTop + valueTextHeight }
                : RectF { pos.x + size - valueWidth, valueTextTop, pos.x + size, valueTextTop + valueTextHeight };
        RectF sliderRect =
            rtl ? RectF { rightRect.right + gapToValue, sliderTop, pos.x + size, sliderTop + sliderHeight }
                : RectF { pos.x, sliderTop, rightRect.left - gapToValue, sliderTop + sliderHeight };
        if (sliderRect.getWidth() < checkboxSize * 3.f) {
            if (rtl)
                sliderRect.left = std::min(sliderRect.left, sliderRect.right - checkboxSize * 3.f);
            else
                sliderRect.right = std::max(sliderRect.right, sliderRect.left + checkboxSize * 3.f);
        }

        float innerPad = 0.2f * sliderRect.getHeight();
        RectF innerSliderRect = { sliderRect.left + innerPad, sliderRect.top + innerPad, sliderRect.right - innerPad,
                                  sliderRect.bottom - innerPad };

        auto latchValue = [&](float newVal) {
            newVal = std::clamp(newVal, min, max);
            if (interval != 0.f) {
                newVal /= interval;
                newVal = std::round(newVal);
                newVal *= interval;
            }
            return newVal;
        };
        auto latchTypedValue = [&](float newVal) {
            return newVal;
        };

        if (kbBind && (shouldSelect(sliderRect, cursorPos) || shouldSelect(rightRect, cursorPos))) {
            kbHoverLabel = group->name() + "." + set->name();
        }
        if (!set->desc().empty() &&
            (shouldSelect(textRect, cursorPos) || shouldSelect(sliderRect, cursorPos) ||
             shouldSelect(rightRect, cursorPos))) {
            setTooltip(set->desc());
        }

        bool valueClicked = false;
        if (justClicked[0]) {
            if (bypassClickThrough ? rightRect.contains(cursorPos) : shouldSelect(rightRect, cursorPos)) {
                valueBox->setSelected(true);
                valueClicked = true;
                playClickSound();
            } else {
                valueBox->setSelected(false);
            }
        }

        if (!this->activeSetting) {
            if (justClicked[0] && !valueClicked &&
                (bypassClickThrough ? sliderRect.contains(cursorPos) : shouldSelect(sliderRect, cursorPos))) {
                activeSetting = set;
                playClickSound();
            }
        } else {
            if (activeSetting == set) {
                if (!mouseButtons[0]) activeSetting = nullptr;

                float find = rtl ? (sliderRect.right - cursorPos.x) / sliderRect.getWidth()
                                 : (cursorPos.x - sliderRect.left) / sliderRect.getWidth();
                float newVal = find * (max - min) + min;
                if (kbBind) {
                    KeybindManager::get().recordEdit(kbEditingBind, group->name(), set->name(),
                                                     (size_t)Setting::Type::Float, latchValue(newVal));
                } else {
                    std::get<FloatValue>(*set->value) = latchValue(newVal);
                    set->update();
                    set->userUpdate();
                }
            }
        }

        if (valueBox->isSelected()) {
            std::wstring typedText = valueBox->getText();
            if (!typedText.empty()) {
                try {
                    float newVal = latchTypedValue(std::stof(typedText));
                    if (kbBind) {
                        if (newVal != dispVal) {
                            KeybindManager::get().recordEdit(kbEditingBind, group->name(), set->name(),
                                                             (size_t)Setting::Type::Float, newVal);
                        }
                    } else if (newVal != std::get<FloatValue>(*set->value)) {
                        std::get<FloatValue>(*set->value) = newVal;
                        set->update();
                        set->userUpdate();
                    }
                } catch (...) {}
            }
        }

        valueBox->setRect(rightRect);
        valueBox->render(dc, round, d2d::Color::RGB(0x8D, 0x8D, 0x8D).asAlpha(0.f), D2D1::ColorF::White);
        if (valueBox->isSelected()) {
            dc.drawRoundedRectangle(rightRect, D2D1::ColorF::White, round, 1.f);
        }

        float range = max - min;
        float percent = range == 0.f ? 0.f : (dispVal - min) / range;
        percent = std::clamp(percent, 0.f, 1.f);
        float oRight = innerSliderRect.right;
        float oLeft = innerSliderRect.left;
        float newStop = 0.f;

        if (activeSetting == set) {
            newStop = cursorPos.x;
        } else {
            newStop = rtl ? sliderRect.right - (sliderRect.getWidth() * percent)
                          : sliderRect.left + (sliderRect.getWidth() * percent);
        }
        if (rtl) {
            innerSliderRect.left = std::clamp(newStop, oLeft, oRight);
        } else {
            innerSliderRect.right = std::clamp(newStop, oLeft, oRight);
        }

        dc.fillRoundedRectangle(sliderRect, d2d::Color::RGB(0x8D, 0x8D, 0x8D).asAlpha(0.11f),
                                sliderRect.getHeight() / 2.f);
        dc.fillRoundedRectangle(innerSliderRect, accentColor, innerSliderRect.getHeight() / 2.f);

        dc.brush->SetColor(d2d::Color(0xB9, 0xB9, 0xB9).get());
        dc.ctx->FillEllipse(D2D1::Ellipse({ rtl ? innerSliderRect.left : innerSliderRect.right, sliderRect.centerY() },
                                          sliderRect.getHeight() * 0.6f, sliderRect.getHeight() * 0.6f),
                            dc.brush);
        return std::max(sliderRect.bottom, rightRect.bottom);
    } break;
    case Setting::Type::Button: {
        auto disp = set->getDisplayName();
        float btnTextSize = textSize * 0.85f;
        float innerPad = checkboxSize * 0.25f;
        float iconSize = checkboxSize - innerPad * 2.f;
        float textWidth = dc.getTextSize(disp, FontSelection::PrimaryRegular, btnTextSize, false, false,
                                         Vec2 { 10000.f, 10000.f })
                              .x;
        float btnWidth = std::min(checkboxSize + textWidth + innerPad * 3.f, size);
        RectF btnRect = d2d::controlAtStart(pos, size, btnWidth, checkboxSize, rtl);

        bool contains = this->shouldSelect(btnRect, cursorPos);

        auto colOff = d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.11f);
        if (!set->rendererInfo.init) {
            set->rendererInfo.init = true;
            set->rendererInfo.col[0] = colOff.r;
            set->rendererInfo.col[1] = colOff.g;
            set->rendererInfo.col[2] = colOff.b;
            set->rendererInfo.col[3] = colOff.a;
        }
        auto lerpedColor = util::LerpColorState(set->rendererInfo.col, colOff + 0.15f, colOff, contains);
        set->rendererInfo.col[0] = lerpedColor.r;
        set->rendererInfo.col[1] = lerpedColor.g;
        set->rendererInfo.col[2] = lerpedColor.b;
        set->rendererInfo.col[3] = lerpedColor.a;

        if (contains && justClicked[0]) {
            set->update();
            set->userUpdate();
            playClickSound();
        }

        dc.fillRoundedRectangle(btnRect, Color(set->rendererInfo.col), round);

        RectF iconRect = rtl ? RectF { btnRect.right - innerPad - iconSize, btnRect.top + innerPad,
                                       btnRect.right - innerPad, btnRect.bottom - innerPad }
                             : RectF { btnRect.left + innerPad, btnRect.top + innerPad,
                                       btnRect.left + innerPad + iconSize, btnRect.bottom - innerPad };
        dc.ctx->DrawBitmap(Necromancer::getAssets().folderIcon.getBitmap(), iconRect);

        RectF textRect = rtl ? RectF { btnRect.left + innerPad * 0.5f, btnRect.top, iconRect.left - innerPad,
                                       btnRect.bottom }
                             : RectF { iconRect.right + innerPad, btnRect.top, btnRect.right - innerPad * 0.5f,
                                       btnRect.bottom };
        dc.drawSingleLineFitted(textRect, disp, d2d::Color(1.f, 1.f, 1.f, 1.f), FontSelection::PrimaryRegular,
                                btnTextSize, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        if (!set->desc().empty() && contains) setTooltip(set->desc());
        if (kbBind && contains) setTooltip(LocalizeString::get("client.ui.clickGui.keybinds.notBindable.desc"));
        return pos.y + checkboxSize;
    } break;
    default:
        return pos.y;
    }
}

bool ClickGUI::shouldSelect(d2d::Rect rc, Vec2 const& pt) {
    if (modClip) {
        if (!modClip.value().contains(pt) || !Screen::shouldSelect(rc, pt)) {
            return false;
        }
    }
    return Screen::shouldSelect(rc, pt);
}

void ClickGUI::drawColorPicker() {
    auto& cursorPos = SDK::ClientInstance::get()->cursorPos;
    const bool rtl = Necromancer::get().getL10nData().isSelectedLanguageRightToLeft();
    D2DUtil dc;
    dc.ctx->SetTarget(auxiliaryBitmap.Get());
    dc.ctx->Clear();

    float rectWidth = 0.2419f * rect.getWidth();
    cPickerRect.right = cPickerRect.left + rectWidth;

    float boxWidth = 0.79f * rectWidth;
    float remPad = (rectWidth - boxWidth) / 2.f;

    // Color PIcker Text
    float textSize = 0.09f * rectWidth;
    RectF titleRect = { cPickerRect.left + remPad, cPickerRect.top + remPad, cPickerRect.right - remPad,
                        cPickerRect.top + remPad + textSize };

    {
        dc.drawSingleLineFitted(titleRect, LocalizeString::get("client.ui.clickGui.colorPicker.name"),
                                { 1.f, 1.f, 1.f, 1.f }, Renderer::FontSelection::PrimaryLight, textSize,
                                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    float boxTop = titleRect.bottom + remPad;

    RectF boxRect = { titleRect.left, boxTop, titleRect.right, boxTop + boxWidth };

    ComPtr<ID2D1LinearGradientBrush> mainColorBrush;
    ComPtr<ID2D1LinearGradientBrush> valueBrush;
    ComPtr<ID2D1LinearGradientBrush> hueBrush;
    ComPtr<ID2D1LinearGradientBrush> alphaBrush;

    // TODO: support chroma, multiple colors
    auto& colVal = std::get<ColorValue>(*colorPicker.setting->value);
    d2d::Color col = util::HSVToColor(colorPicker.pickerColor);
    d2d::Color sCol = { colorPicker.selectedColor->r, colorPicker.selectedColor->g, colorPicker.selectedColor->b,
                        colorPicker.selectedColor->a };
    d2d::Color nsCol = util::HSVToColor({ util::ColorToHSV(sCol).h, 1.f, 1.f });
    d2d::Color baseCol = util::HSVToColor({ colorPicker.pickerColor.h, 1.f, 1.f });

    // main brush
    {
        ComPtr<ID2D1GradientStopCollection> gradientStopCollection;

        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES prop {};
        auto ss = Necromancer::getRenderer().getScreenSize();
        prop.startPoint = { boxRect.left, boxRect.top };
        prop.endPoint = { boxRect.right, boxRect.top };

        const D2D1_GRADIENT_STOP stops[] = { 0.f, { 1.f, 1.f, 1.f, 1.f }, 1.f, baseCol.get() };

        dc.ctx->CreateGradientStopCollection(stops, _countof(stops), gradientStopCollection.GetAddressOf());
        dc.ctx->CreateLinearGradientBrush(prop, gradientStopCollection.Get(), mainColorBrush.GetAddressOf());
    }

    // Value brush
    {
        ComPtr<ID2D1GradientStopCollection> gradientStopCollection;

        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES prop {};
        auto ss = Necromancer::getRenderer().getScreenSize();
        prop.startPoint = { boxRect.left, boxRect.bottom };
        prop.endPoint = { boxRect.left, boxRect.top };

        const D2D1_GRADIENT_STOP stops[] = { 0.f, { 0.f, 0.f, 0.f, 1.f }, 1.f, { 0.f, 0.f, 0.f, 0.f } };

        dc.ctx->CreateGradientStopCollection(stops, _countof(stops), gradientStopCollection.GetAddressOf());
        dc.ctx->CreateLinearGradientBrush(prop, gradientStopCollection.Get(), valueBrush.GetAddressOf());
    }
    // Draw inner part of colorpicker

    dc.fillRectangle(boxRect, mainColorBrush.Get());
    dc.fillRectangle(boxRect, valueBrush.Get());
    dc.drawRectangle(boxRect, d2d::Color::RGB(0x50, 0x50, 0x50).asAlpha(0.28f), 2.f);

    float hueBarHeight = boxRect.getHeight() * 0.0506329f;

    float padToHueBar = remPad * 0.6f;

    RectF hueBar = { boxRect.left, boxRect.bottom + padToHueBar, boxRect.right,
                     boxRect.bottom + hueBarHeight + padToHueBar };

    // Hue brush
    {
        ComPtr<ID2D1GradientStopCollection> gradientStopCollection;

        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES prop {};
        auto ss = Necromancer::getRenderer().getScreenSize();
        prop.startPoint = { hueBar.left, hueBar.top };
        prop.endPoint = { hueBar.right, hueBar.top };

        const D2D1_GRADIENT_STOP stops[] = {
            { 0.f, d2d::Color(util::HSVToColor({ 0.f, 1.f, 1.f })).get() },
            { 1.f / 7.f, d2d::Color(util::HSVToColor({ (1.f / 7.f) * 360.f, 1.f, 1.f })).get() },
            { 2.f / 7.f, d2d::Color(util::HSVToColor({ (2.f / 7.f) * 360.f, 1.f, 1.f })).get() },
            { 3.f / 7.f, d2d::Color(util::HSVToColor({ (3.f / 7.f) * 360.f, 1.f, 1.f })).get() },
            { 4.f / 7.f, d2d::Color(util::HSVToColor({ (4.f / 7.f) * 360.f, 1.f, 1.f })).get() },
            { 5.f / 7.f, d2d::Color(util::HSVToColor({ (5.f / 7.f) * 360.f, 1.f, 1.f })).get() },
            { 6.f / 7.f, d2d::Color(util::HSVToColor({ (6.f / 7.f) * 360.f, 1.f, 1.f })).get() },
            { 1.f, d2d::Color(util::HSVToColor({ 0.f, 1.f, 1.f })).get() },
        };

        dc.ctx->CreateGradientStopCollection(stops, 8, gradientStopCollection.GetAddressOf());
        dc.ctx->CreateLinearGradientBrush(prop, gradientStopCollection.Get(), hueBrush.GetAddressOf());
    }

    dc.fillRoundedRectangle(hueBar, hueBrush.Get(), hueBar.getHeight() / 2.f);
    dc.drawRoundedRectangle(hueBar, d2d::Color::RGB(0x50, 0x50, 0x50).asAlpha(0.28f), hueBar.getHeight() / 2.f,
                            hueBar.getHeight() / 4.f, DrawUtil::OutlinePosition::Outside);

    RectF alphaBar = { hueBar.left, hueBar.bottom + padToHueBar, hueBar.right,
                       hueBar.bottom + padToHueBar + hueBarHeight };

    // Alpha brush
    {
        ComPtr<ID2D1GradientStopCollection> gradientStopCollection;

        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES prop {};
        auto ss = Necromancer::getRenderer().getScreenSize();
        prop.startPoint = { alphaBar.left, alphaBar.top };
        prop.endPoint = { alphaBar.right, alphaBar.top };

        const D2D1_GRADIENT_STOP stops[] = { { 0.f, col.asAlpha(0.f).get() }, { 1.f, col.asAlpha(1.f).get() } };

        dc.ctx->CreateGradientStopCollection(stops, _countof(stops), gradientStopCollection.GetAddressOf());
        dc.ctx->CreateLinearGradientBrush(prop, gradientStopCollection.Get(), alphaBrush.GetAddressOf());
    }

    dc.fillRoundedRectangle(alphaBar, { 1.f, 1.f, 1.f, 0.5f }, alphaBar.getHeight() / 2.f);

    drawAlphaBar(dc, alphaBar, alphaBar.getHeight() / 2.f, 2);
    // dc.fillRoundedRectangle(hueBar, hueBrush.Get(), hueBar.getHeight() / 2.f);
    dc.fillRoundedRectangle(alphaBar, alphaBrush.Get(), alphaBar.getHeight() / 2.f);
    dc.drawRoundedRectangle(alphaBar, d2d::Color::RGB(0x37, 0x37, 0x37).asAlpha(0.88f), alphaBar.getHeight() / 2.f,
                            alphaBar.getHeight() / 3.f, DrawUtil::OutlinePosition::Outside);

    // color hex edits/displays

    std::array<std::optional<StoredColor>, 3> cols = { colVal.getMainColor(), std::nullopt, std::nullopt };

    if (colVal.isChroma) {
        cols[1] = colVal.color2;
        cols[2] = colVal.color3;
    }

    RectF lastrc = alphaBar;
    for (size_t i = 0; i < cols.size(); ++i) {
        auto& c = cols[i];
        if (c.has_value()) {
            float colorModeWidth = alphaBar.getWidth() / 4.f;
            float hexBoxWidth = alphaBar.getWidth() * 0.617f;
            float boxHeight = alphaBar.getHeight() * 2.f;
            float colorDisplayWidth = boxHeight;

            float pad = (alphaBar.getWidth() - colorModeWidth - hexBoxWidth - colorDisplayWidth) / 3.f;

            RectF totalDisplayRect = lastrc.translate(0.f, padToHueBar);
            totalDisplayRect.bottom = totalDisplayRect.top + boxHeight;
            lastrc = totalDisplayRect;
            RectF colorModeRect = { totalDisplayRect.left, totalDisplayRect.top, totalDisplayRect.left + colorModeWidth,
                                    totalDisplayRect.bottom };
            RectF hexBox = { colorModeRect.right + pad, totalDisplayRect.top, colorModeRect.right + pad + hexBoxWidth,
                             totalDisplayRect.bottom };
            RectF colorDisplayRect = { totalDisplayRect.right - pad - colorDisplayWidth, totalDisplayRect.top,
                                       totalDisplayRect.right - pad, totalDisplayRect.bottom };

            if (pickerTextBoxes.size() <= i) {
                pickerTextBoxes.insert(pickerTextBoxes.begin() + i, TextBox(hexBox, 7));
                Necromancer::get().addTextBox(&pickerTextBoxes[i]);
            }
            auto& tb = pickerTextBoxes[i];

            auto bgCol = d2d::Color::RGB(0x50, 0x50, 0x50).asAlpha(0.28f);

            auto round = 0.1875f * colorModeRect.getHeight();

            dc.fillRoundedRectangle(colorModeRect, bgCol, round);
            // dc.fillRoundedRectangle(hexBox, bgCol, round);
            dc.fillRoundedRectangle(colorDisplayRect, col.asAlpha(colorPicker.opacityMod), round);

            std::wstring alphaTxt = util::StrToWStr(std::format("{:.2f}", colorPicker.opacityMod));

            dc.drawText(colorDisplayRect, alphaTxt,
                        (colorPicker.opacityMod < 0.5f || colorPicker.pickerColor.v < 0.5f) ? D2D1::ColorF::White
                                                                                            : D2D1::ColorF::Black,
                        Renderer::FontSelection::PrimaryRegular, colorDisplayRect.getHeight() * 0.5f,
                        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            tb.setRect(hexBox);

            if (!tb.isSelected()) {
                tb.setText(util::StrToWStr("#" + col.getHex()));
            }

            tb.render(dc, round, bgCol, D2D1::ColorF::White);
            if (tb.isSelected()) {
                d2d::Color newCol = col;
                std::string txt = util::WStrToStr(tb.getText());
                if (txt[0] == '#') {
                    txt = txt.substr(1);
                }
                if (txt.size() == 6) try {
                        newCol = d2d::Color::Hex(txt);
                    } catch (...) {}

                auto newHSV = util::ColorToHSV(newCol);
                colorPicker.svModX = newHSV.s;
                colorPicker.svModY = 1.f - newHSV.v;
                colorPicker.hueMod = newHSV.h / 360.f;
            } else {
                tb.setCaretLocation(static_cast<int>(tb.getText().size()));
            }

            if (justClicked[0]) {
                tb.setSelected(hexBox.contains(cursorPos));
            }

            // rgb setting
            colorPicker.rgbSetting.value = &colorPicker.rgbSelector;
            float rgbBottom = drawSetting(&colorPicker.rgbSetting, nullptr,
                                          { alphaBar.left, alphaBar.bottom + hexBox.getHeight() * 1.5f }, dc, 150.f,
                                          0.21f, true);

            if (colorPicker.setting->supportsTagColor) {
                colorPicker.forceTagSetting.value = &colorPicker.forceTagSelector;
                drawSetting(&colorPicker.forceTagSetting, nullptr,
                            { alphaBar.left, rgbBottom + hexBox.getHeight() * 0.5f }, dc, 150.f, 0.21f, true);
            }
        }
    }

    float ellipseRadius = 0.75f * alphaBar.getHeight();

    // sv
    if (colorPicker.isEditingSV || (justClicked[0] && boxRect.contains(cursorPos))) {
        colorPicker.svModX =
            std::max(std::min(cursorPos.x - boxRect.left, boxRect.getWidth()) / boxRect.getWidth(), 0.f);
        colorPicker.svModY =
            std::max(std::min(cursorPos.y - boxRect.top, boxRect.getHeight()) / boxRect.getHeight(), 0.f);

        colorPicker.isEditingSV = true;
    }

    // hue
    if (colorPicker.isEditingHue || (justClicked[0] && hueBar.contains(cursorPos))) {
        colorPicker.hueMod = std::max(std::min(cursorPos.x - hueBar.left, hueBar.getWidth()) / hueBar.getWidth(), 0.f);
        colorPicker.isEditingHue = true;
    }

    // alpha
    if (colorPicker.isEditingOpacity || (justClicked[0] && alphaBar.contains(cursorPos))) {
        colorPicker.opacityMod =
            std::max(std::min(cursorPos.x - alphaBar.left, alphaBar.getWidth()) / alphaBar.getWidth(), 0.f);

        float val = colorPicker.opacityMod;

        float interval = 0.05f;

        // Find a good value to set to ("latch to nearest")
        val /= interval;
        val = std::round(val);
        val *= interval;

        colorPicker.opacityMod = val;
        colorPicker.isEditingOpacity = true;
    }

    if (!mouseButtons[0]) {
        colorPicker.isEditingSV = false;
        colorPicker.isEditingHue = false;
        colorPicker.isEditingOpacity = false;
    }

    {
        colorPicker.pickerColor.h = (colorPicker.hueMod * 360.f);
        colorPicker.pickerColor.s = colorPicker.svModX;
        colorPicker.pickerColor.v = 1.f - colorPicker.svModY;
    }

    // SV
    {
        auto ellipse = D2D1::Ellipse({ boxRect.left + (hueBar.getWidth() * colorPicker.svModX),
                                       boxRect.top + (boxRect.getHeight() * colorPicker.svModY) },
                                     ellipseRadius, ellipseRadius);
        dc.brush->SetColor(col.asAlpha(1.f).get());
        dc.ctx->FillEllipse(ellipse, dc.brush);
        dc.brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
        dc.ctx->DrawEllipse(ellipse, dc.brush, ellipseRadius / 2.f);
    }

    // hue
    {
        auto ellipse = D2D1::Ellipse({ hueBar.left + (hueBar.getWidth() * colorPicker.hueMod), hueBar.centerY() },
                                     ellipseRadius, ellipseRadius);
        auto huedCol = util::HSVToColor({ colorPicker.hueMod * 360.f, 1.f, 1.f });
        dc.brush->SetColor(d2d::Color(huedCol).get());
        dc.ctx->FillEllipse(ellipse, dc.brush);
        dc.brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
        dc.ctx->DrawEllipse(ellipse, dc.brush, ellipseRadius / 2.f);
    }

    // alpha
    {
        auto ellipse =
            D2D1::Ellipse({ alphaBar.left + (alphaBar.getWidth() * colorPicker.opacityMod), alphaBar.centerY() },
                          ellipseRadius, ellipseRadius);
        dc.brush->SetColor(col.asAlpha(colorPicker.opacityMod).get());
        dc.ctx->FillEllipse(ellipse, dc.brush);
        dc.brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
        dc.ctx->DrawEllipse(ellipse, dc.brush, ellipseRadius / 2.f);
    }

    cPickerRect.bottom = alphaBar.bottom + remPad * 2.f + 50.f;
    if (colorPicker.setting->supportsTagColor) {
        cPickerRect.bottom += rect.getWidth() * setting_height_relative + remPad;
    }

    dc.ctx->SetTarget(Necromancer::getRenderer().getBitmap());

    // draw menu

    dc.fillRoundedRectangle(cPickerRect, d2d::Color::RGB(0x7, 0x7, 0x7).asAlpha(0.8f), 19.f * adaptedScale);
    dc.drawRoundedRectangle(cPickerRect, d2d::Color::RGB(0, 0, 0).asAlpha(0.28f), 19.f * adaptedScale,
                            4.f * adaptedScale, DrawUtil::OutlinePosition::Outside);

    // x button
    float xWidth = 0.06f * rectWidth;
    RectF xRect = rtl ? RectF { cPickerRect.left + xWidth, cPickerRect.top + xWidth, cPickerRect.left + xWidth * 2.f,
                                cPickerRect.top + xWidth * 2.f }
                      : RectF { cPickerRect.right - xWidth * 2.f, cPickerRect.top + xWidth, cPickerRect.right - xWidth,
                                cPickerRect.top + xWidth * 2.f };
    dc.ctx->DrawBitmap(Necromancer::getAssets().xIcon.getBitmap(), xRect);

    if (justClicked[0] && xRect.contains(cursorPos)) {
        colorPicker.queueClose = true;
        playClickSound();
    }

    // inner contents
    dc.ctx->DrawBitmap(auxiliaryBitmap.Get());

    RectF pickerTopBar = { cPickerRect.left, cPickerRect.top, cPickerRect.right, boxRect.top };

    if (!colorPicker.dragging && justClicked[0] && pickerTopBar.contains(cursorPos)) {
        colorPicker.dragging = true;
        colorPicker.dragOffs = cursorPos - cPickerRect.getPos();
    }

    if (!mouseButtons[0]) colorPicker.dragging = false;

    if (colorPicker.dragging) {
        cPickerRect.setPos(cursorPos - colorPicker.dragOffs);
    }

    auto ss = Necromancer::getRenderer().getScreenSize();
    util::KeepInBounds(cPickerRect, { 0.f, 0.f, ss.width, ss.height });
}

void ClickGUI::openBlockPicker(BlockESP* mod) {
    if (!mod || blockPicker.mod == mod) return;
    blockPicker = BlockPicker();
    blockPicker.mod = mod;
    if (!blockSearchRegistered) {
        Necromancer::get().addTextBox(&blockSearchBox);
        blockSearchRegistered = true;
    }
    blockSearchBox.reset();
    blockSearchBox.setSelected(false);
    mod->rebuildCatalog();
    float w = 0.31f * rect.getWidth();
    float h = 0.55f * rect.getHeight();
    bPickerRect = { rect.center().x - w * 0.5f, rect.center().y - h * 0.5f, rect.center().x + w * 0.5f,
                    rect.center().y + h * 0.5f };
}

void ClickGUI::closeBlockPicker() {
    if (!blockPicker.mod) return;
    auto mod = blockPicker.mod;
    for (auto& e : mod->getEntries()) {
        if (colorPicker.setting &&
            (colorPicker.setting == e->colorSetting.get() || colorPicker.setting == e->thicknessSetting.get())) {
            colorPicker = ColorPicker();
        }
        if (activeSetting && (activeSetting == e->colorSetting.get() || activeSetting == e->thicknessSetting.get())) {
            activeSetting = nullptr;
        }
    }
    mod->clearIconDraws();
    mod->persist();
    blockSearchBox.setSelected(false);
    blockSearchBox.reset();
    blockPicker = BlockPicker();
    bPickerRect = {};
}

void ClickGUI::openItemSwitcher(ItemSwitcher* mod) {
    if (!mod) return;
    itemSwitcherPicker.mod = mod;
    itemSwitcherPicker.scroll = 0.f;
    itemSwitcherPicker.scrollMax = 0.f;
    itemSwitcherSearchBox.reset();
    itemSwitcherSearchBox.setSelected(false);
    if (!itemSwitcherSearchRegistered) {
        Necromancer::get().addTextBox(&itemSwitcherSearchBox);
        itemSwitcherSearchRegistered = true;
    }
}

void ClickGUI::closeItemSwitcher() {
    if (!itemSwitcherPicker.mod) return;
    itemSwitcherSearchBox.setSelected(false);
    itemSwitcherSearchBox.reset();
    itemSwitcherPicker = ItemSwitcherPicker();
}

void ClickGUI::drawItemSwitcher(D2DUtil& dc) {
    auto mod = itemSwitcherPicker.mod;
    if (!mod) return;

    auto& cursorPos = SDK::ClientInstance::get()->cursorPos;
    auto accentColor = d2d::Color(Necromancer::get().getAccentColor().getMainColor());

    float w = rect.getWidth() * 0.28f;
    float pad = w * 0.04f;
    float titleH = rect.getHeight() * 0.043f;
    float rowH = rect.getHeight() * 0.052f;
    float gap = rowH * 0.14f;
    float searchH = rowH * 1.1f;

    d2d::Rect pickerRect = {
        rect.centerX() - w * 0.5f, rect.centerY() - rect.getHeight() * 0.32f,
        rect.centerX() + w * 0.5f, rect.centerY() + rect.getHeight() * 0.32f
    };
    dc.fillRoundedRectangle(pickerRect, d2d::Color::RGB(0x7, 0x7, 0x7).asAlpha(0.95f), 12.f);
    dc.drawRoundedRectangle(pickerRect, d2d::Color(1.f, 1.f, 1.f, 0.12f), 12.f, 1.f, DrawUtil::OutlinePosition::Inside);

    float top = pickerRect.top + pad;
    float left = pickerRect.left + pad;
    float right = pickerRect.right - pad;

    std::wstring title = LocalizeString::get("client.ui.clickGui.itemSwitcherPicker.title").value();
    d2d::Rect titleRect = { left, top, right, top + titleH };
    dc.drawText(titleRect, title, d2d::Color(1.f, 1.f, 1.f, 1.f), FontSelection::PrimarySemilight, titleH * 0.55f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    float searchTop = titleRect.bottom + pad * 0.5f;
    d2d::Rect searchRect = { left, searchTop, right, searchTop + searchH };
    std::wstring searchText = itemSwitcherSearchBox.getText();
    bool searchSelected = itemSwitcherSearchBox.isSelected();

    if (justClicked[0] && searchRect.contains(cursorPos)) {
        itemSwitcherSearchBox.setSelected(true);
    } else if (justClicked[0]) {
        itemSwitcherSearchBox.setSelected(false);
    }

    dc.fillRoundedRectangle(searchRect, d2d::Color::RGB(0x8D, 0x8D, 0x8D).asAlpha(0.15f), searchRect.getHeight() * 0.22f);
    std::wstring displayText = searchText;
    if (displayText.empty() && !searchSelected) {
        displayText = LocalizeString::get("client.ui.clickGui.itemSwitcherPicker.search").value();
    }
    dc.drawText(searchRect, displayText, searchText.empty() && !searchSelected ? d2d::Color(1.f, 1.f, 1.f, 0.3f) : d2d::Color(1.f, 1.f, 1.f, 1.f),
                FontSelection::PrimaryRegular, searchH * 0.45f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    float listTop = searchRect.bottom + pad * 0.5f;
    float listBottom = pickerRect.bottom - pad;
    d2d::Rect listRect = { left, listTop, right, listBottom };
    dc.ctx->PushAxisAlignedClip(listRect.get(), D2D1_ANTIALIAS_MODE_ALIASED);

    std::wstring filter;
    if (!searchText.empty()) {
        filter = searchText;
        for (auto& c : filter) c = towlower(c);
    }

    auto& entries = ItemCatalog::get().entries();
    std::wstring targetId = mod ? std::get<TextValue>(mod->targetItem).str : L"";

    float contentY = listRect.top - itemSwitcherPicker.scroll;
    float rowWidth = listRect.getWidth();
    itemSwitcherPicker.scrollMax = (std::max)(0.f, static_cast<float>(entries.size()) * (rowH + gap) - listRect.getHeight());

    for (auto& entry : entries) {
        if (!filter.empty() && entry.searchKey.find(filter) == std::wstring::npos) continue;

        d2d::Rect rowRect = { listRect.left, contentY, listRect.left + rowWidth, contentY + rowH };
        if (rowRect.bottom > listRect.top && rowRect.top < listRect.bottom) {
            bool selected = entry.id.size() == targetId.size() && std::equal(entry.id.begin(), entry.id.end(), targetId.begin(), targetId.end(), [](char c, wchar_t wc) { return c == wc; });
            if (selected) {
                dc.fillRoundedRectangle(rowRect, accentColor.asAlpha(0.3f), rowRect.getHeight() * 0.22f);
            } else if (rowRect.contains(cursorPos)) {
                dc.fillRoundedRectangle(rowRect, d2d::Color(1.f, 1.f, 1.f, 0.08f), rowRect.getHeight() * 0.22f);
            }

            if (justClicked[0] && rowRect.contains(cursorPos)) {
                std::wstring wideId(entry.id.begin(), entry.id.end());
                std::get<TextValue>(mod->targetItem).str = wideId;
                closeItemSwitcher();
                dc.ctx->PopAxisAlignedClip();
                return;
            }

            float textLeft = rowRect.left + rowH * 0.15f;
            d2d::Rect textRect = { textLeft, rowRect.top, rowRect.right, rowRect.bottom };
            dc.drawText(textRect, entry.displayName, d2d::Color(1.f, 1.f, 1.f, selected ? 1.f : 0.85f), FontSelection::PrimaryRegular, rowH * 0.4f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        contentY += rowH + gap;
    }

    dc.ctx->PopAxisAlignedClip();
}

void ClickGUI::drawKindPicker(D2DUtil& dc) {
    auto& mgr = KeybindManager::get();
    auto& cursorPos = SDK::ClientInstance::get()->cursorPos;
    auto accentColor = d2d::Color(Necromancer::get().getAccentColor().getMainColor());

    float w = rect.getWidth() * 0.24f;
    float pad = w * 0.04f;
    float titleH = rect.getHeight() * 0.043f;
    float rowH = rect.getHeight() * 0.055f;
    float gap = rowH * 0.14f;

    kbKindPickerRect.right = kbKindPickerRect.left + w;
    kbKindPickerRect.bottom = kbKindPickerRect.top + pad + titleH + pad + rowH * 2.f + gap + pad;

    if (kbKindPickerRect.right > rect.right) {
        float shift = kbKindPickerRect.right - rect.right;
        kbKindPickerRect.left -= shift;
        kbKindPickerRect.right -= shift;
    }
    if (kbKindPickerRect.bottom > rect.bottom) {
        float shift = kbKindPickerRect.bottom - rect.bottom;
        kbKindPickerRect.top -= shift;
        kbKindPickerRect.bottom -= shift;
    }

    dc.fillRoundedRectangle(kbKindPickerRect, d2d::Color::RGB(0x07, 0x07, 0x07).asAlpha(0.95f), 14.f * adaptedScale);
    dc.drawRoundedRectangle(kbKindPickerRect, accentColor.asAlpha(0.72f), 14.f * adaptedScale, 1.5f * adaptedScale,
                            DrawUtil::OutlinePosition::Inside);

    d2d::Rect titleRc { kbKindPickerRect.left + pad, kbKindPickerRect.top + pad, kbKindPickerRect.right - pad,
                        kbKindPickerRect.top + pad + titleH };
    dc.drawSingleLineFitted(titleRc, LocalizeString::get("client.ui.clickGui.keybinds.kind.title.name"),
                            d2d::Colors::WHITE, FontSelection::PrimarySemilight, titleH * 0.5f,
                            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    float y = titleRc.bottom + pad;
    d2d::Rect normalRc { kbKindPickerRect.left + pad, y, kbKindPickerRect.right - pad, y + rowH };
    d2d::Rect ifRc { normalRc.left, normalRc.bottom + gap, normalRc.right, normalRc.bottom + gap + rowH };

    bool overPicker = kbKindPickerRect.contains(cursorPos);
    int chosen = -1;

    auto row = [&](d2d::Rect const& rc, std::wstring const& label, std::wstring const& tip) {
        bool hovered = overPicker && rc.contains(cursorPos);
        auto bg = hovered ? accentColor : d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.11f);
        dc.fillRoundedRectangle(rc, bg, rc.getHeight() * 0.25f);
        dc.drawSingleLineFitted(rc, label, d2d::Colors::WHITE, FontSelection::PrimaryRegular, rc.getHeight() * 0.42f,
                                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (hovered && !tip.empty()) setTooltip(tip);
        return hovered && justClicked[0] && !kbKindPicker.justOpened;
    };

    if (row(normalRc, LocalizeString::get("client.ui.clickGui.keybinds.kind.normal.name").value(),
            LocalizeString::get("client.ui.clickGui.keybinds.kind.normal.desc").value())) {
        chosen = KeybindManager::KindNormal;
    }
    if (row(ifRc, LocalizeString::get("client.ui.clickGui.keybinds.kind.if.name").value(),
            LocalizeString::get("client.ui.clickGui.keybinds.kind.if.desc").value())) {
        chosen = KeybindManager::KindIf;
    }

    if (chosen >= 0) {
        int n = 1;
        std::string candidate;
        std::string prefix = chosen == KeybindManager::KindIf ? "If " : "Bind ";
        do {
            candidate = prefix + std::to_string(n++);
        } while (mgr.findBind(candidate));

        if (mgr.createBind(candidate, chosen) && chosen == KeybindManager::KindIf) openCondCanvas(candidate);
        kbKindPicker.queueClose = true;
        playClickSound();
        return;
    }

    if (kbKindPicker.justOpened) {
        kbKindPicker.justOpened = false;
    } else if (justClicked[0] && !kbKindPickerRect.contains(cursorPos)) {
        kbKindPicker.queueClose = true;
    }
}

void ClickGUI::drawParentPicker(D2DUtil& dc) {
    auto& mgr = KeybindManager::get();
    auto* bind = mgr.findBind(kbParentPicker.bind);
    if (!bind) {
        kbParentPicker.queueClose = true;
        return;
    }

    auto& cursorPos = SDK::ClientInstance::get()->cursorPos;
    auto accentColor = d2d::Color(Necromancer::get().getAccentColor().getMainColor());

    std::vector<std::string> options;
    options.emplace_back();
    for (auto& other : mgr.getBinds()) {
        if (other.name == bind->name) continue;
        if (!mgr.canBeParentOf(bind->name, other.name)) continue;
        options.push_back(other.name);
    }

    float w = 0.26f * rect.getWidth();
    float pad = 0.032f * w;
    float titleH = 0.085f * w;
    float rowH = rect.getWidth() * setting_height_relative * 1.3f;
    float rowGap = rowH * 0.12f;
    float maxListH = 0.34f * rect.getHeight();
    float fullListH = static_cast<float>(options.size()) * (rowH + rowGap) - (options.empty() ? 0.f : rowGap);
    float listH = std::min(maxListH, std::max(rowH, fullListH));

    kbParentPickerRect.right = kbParentPickerRect.left + w;
    kbParentPickerRect.bottom = kbParentPickerRect.top + pad + titleH + pad + listH + pad;

    if (kbParentPickerRect.bottom > rect.bottom) {
        float shift = kbParentPickerRect.bottom - rect.bottom;
        kbParentPickerRect.top -= shift;
        kbParentPickerRect.bottom -= shift;
    }
    if (kbParentPickerRect.top < rect.top) {
        float shift = rect.top - kbParentPickerRect.top;
        kbParentPickerRect.top += shift;
        kbParentPickerRect.bottom += shift;
    }
    if (kbParentPickerRect.right > rect.right) {
        float shift = kbParentPickerRect.right - rect.right;
        kbParentPickerRect.left -= shift;
        kbParentPickerRect.right -= shift;
    }
    if (kbParentPickerRect.left < rect.left) {
        float shift = rect.left - kbParentPickerRect.left;
        kbParentPickerRect.left += shift;
        kbParentPickerRect.right += shift;
    }

    dc.fillRoundedRectangle(kbParentPickerRect, d2d::Color::RGB(0x7, 0x7, 0x7).asAlpha(0.92f), 19.f * adaptedScale);
    dc.drawRoundedRectangle(kbParentPickerRect, d2d::Color::RGB(0, 0, 0).asAlpha(0.28f), 19.f * adaptedScale,
                            4.f * adaptedScale, DrawUtil::OutlinePosition::Outside);

    d2d::Rect titleRect = { kbParentPickerRect.left + pad, kbParentPickerRect.top + pad,
                            kbParentPickerRect.right - pad - titleH, kbParentPickerRect.top + pad + titleH };
    dc.drawSingleLineFitted(titleRect,
                            LocalizeString::get("client.ui.clickGui.keybinds.parent.title.name").value() + L" \x2014 " +
                                util::StrToWStr(bind->name),
                            { 1.f, 1.f, 1.f, 1.f }, Renderer::FontSelection::PrimaryLight, titleH * 0.7f,
                            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    d2d::Rect xRect = { kbParentPickerRect.right - pad - titleH * 0.8f, kbParentPickerRect.top + pad + titleH * 0.1f,
                        kbParentPickerRect.right - pad, kbParentPickerRect.top + pad + titleH * 0.9f };
    dc.ctx->DrawBitmap(Necromancer::getAssets().xIcon.getBitmap(), xRect.get());
    if (justClicked[0] && xRect.contains(cursorPos)) {
        kbParentPicker.queueClose = true;
        playClickSound();
    }

    d2d::Rect listArea = { kbParentPickerRect.left + pad, titleRect.bottom + pad, kbParentPickerRect.right - pad,
                           kbParentPickerRect.bottom - pad };

    kbParentPicker.scrollMax = std::max(0.f, fullListH - listArea.getHeight());
    kbParentPicker.scroll = std::clamp(kbParentPicker.scroll, 0.f, kbParentPicker.scrollMax);
    kbParentPicker.lerpScroll = std::lerp(kbParentPicker.lerpScroll, kbParentPicker.scroll,
                                          Necromancer::getRenderer().getDeltaTime() / 5.f);

    dc.ctx->PushAxisAlignedClip(listArea.get(), D2D1_ANTIALIAS_MODE_ALIASED);

    bool inList = listArea.contains(cursorPos);
    float y = listArea.top - kbParentPicker.lerpScroll;
    std::string chosen;
    bool hasChoice = false;

    for (auto& option : options) {
        d2d::Rect rowRect = { listArea.left, y, listArea.right, y + rowH };
        if (rowRect.bottom > listArea.top && rowRect.top < listArea.bottom) {
            bool selected = option == bind->parent;
            bool hovered = inList && rowRect.contains(cursorPos);
            auto bg = selected ? accentColor.asAlpha(0.55f)
                : hovered      ? d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.18f)
                               : d2d::Color::RGB(0x8D, 0x8D, 0x8D).asAlpha(0.10f);
            dc.fillRoundedRectangle(rowRect, bg, rowRect.getHeight() * 0.25f);

            std::wstring label = option.empty()
                ? LocalizeString::get("client.ui.clickGui.keybinds.parent.none.name").value()
                : util::StrToWStr(option);
            d2d::Rect textRect = { rowRect.left + pad * 0.6f, rowRect.top, rowRect.right - pad * 0.6f,
                                   rowRect.bottom };
            dc.drawSingleLineFitted(textRect, label, d2d::Colors::WHITE, FontSelection::PrimaryRegular,
                                    rowH * 0.38f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            if (hovered && justClicked[0] && !kbParentPicker.justOpened) {
                chosen = option;
                hasChoice = true;
            }
        }
        y += rowH + rowGap;
    }

    dc.ctx->PopAxisAlignedClip();

    if (hasChoice) {
        mgr.setParent(bind->name, chosen);
        kbParentPicker.queueClose = true;
        playClickSound();
        return;
    }

    if (kbParentPicker.justOpened) {
        kbParentPicker.justOpened = false;
    } else if (justClicked[0] && !kbParentPickerRect.contains(cursorPos)) {
        kbParentPicker.queueClose = true;
    }
}

void ClickGUI::drawBlockPicker(D2DUtil& dc) {
    auto mod = blockPicker.mod;
    if (!mod) return;

    auto& cursorPos = SDK::ClientInstance::get()->cursorPos;
    auto accentColor = d2d::Color(Necromancer::get().getAccentColor().getMainColor());

    bool savedClick = justClicked[0];
    if (colorPicker.setting) justClicked[0] = false;

    float w = 0.31f * rect.getWidth();
    float pad = 0.032f * w;
    float titleH = 0.085f * w;
    float rowH = rect.getWidth() * setting_height_relative * 1.6f;
    float rowGap = rowH * 0.15f;
    float listH = 0.42f * rect.getHeight();

    bPickerRect.right = bPickerRect.left + w;
    bPickerRect.bottom = bPickerRect.top + pad + titleH + pad + rowH + pad + listH + pad;

    dc.fillRoundedRectangle(bPickerRect, d2d::Color::RGB(0x7, 0x7, 0x7).asAlpha(0.92f), 19.f * adaptedScale);
    dc.drawRoundedRectangle(bPickerRect, d2d::Color::RGB(0, 0, 0).asAlpha(0.28f), 19.f * adaptedScale,
                            4.f * adaptedScale, DrawUtil::OutlinePosition::Outside);

    RectF titleRect = { bPickerRect.left + pad, bPickerRect.top + pad, bPickerRect.right - pad - titleH,
                        bPickerRect.top + pad + titleH };
    dc.drawSingleLineFitted(titleRect, mod->getDisplayName() + L" \x2014 " +
                                           LocalizeString::get("client.ui.clickGui.blockPicker.name").value(),
                            { 1.f, 1.f, 1.f, 1.f }, Renderer::FontSelection::PrimaryLight, titleH * 0.75f,
                            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    RectF xRect = { bPickerRect.right - pad - titleH * 0.8f, bPickerRect.top + pad + titleH * 0.1f,
                    bPickerRect.right - pad, bPickerRect.top + pad + titleH * 0.9f };
    dc.ctx->DrawBitmap(Necromancer::getAssets().xIcon.getBitmap(), xRect);
    if (justClicked[0] && xRect.contains(cursorPos)) {
        blockPicker.queueClose = true;
        playClickSound();
    }

    float controlsTop = titleRect.bottom + pad;
    RectF controlsRect = { bPickerRect.left + pad, controlsTop, bPickerRect.right - pad, controlsTop + rowH };
    RectF listArea = { bPickerRect.left + pad, controlsRect.bottom + pad, bPickerRect.right - pad,
                       bPickerRect.bottom - pad };

    auto localButton = [&](RectF const& rc, std::wstring const& text, bool enabled) {
        bool hovered = enabled && rc.contains(cursorPos);
        auto bg = enabled ? (hovered ? accentColor : d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.11f))
                          : d2d::Color::RGB(0x70, 0x70, 0x70).asAlpha(0.11f);
        dc.fillRoundedRectangle(rc, bg, rc.getHeight() * 0.25f);
        dc.drawSingleLineFitted(rc, text, enabled ? d2d::Color(1.f, 1.f, 1.f, 1.f) : d2d::Color(1.f, 1.f, 1.f, 0.32f),
                                FontSelection::PrimaryRegular, rc.getHeight() * 0.42f, DWRITE_TEXT_ALIGNMENT_CENTER,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (hovered && justClicked[0]) {
            playClickSound();
            return true;
        }
        return false;
    };

    std::vector<BlockESP::IconDraw> icons;
    auto pushIcon = [&](RectF const& cell, void* block) {
        if (!block) return;
        RectF snapped { std::round(cell.left), std::round(cell.top), std::round(cell.right),
                        std::round(cell.bottom) };
        if (snapped.getWidth() < 1.f || snapped.getHeight() < 1.f) return;
        if (snapped.top < listArea.top - 0.5f || snapped.bottom > listArea.bottom + 0.5f ||
            snapped.left < listArea.left - 0.5f || snapped.right > listArea.right + 0.5f) {
            return;
        }
        icons.push_back({ snapped, block });
    };

    float contentH = 0.f;
    auto& entries = mod->getEntries();

    if (!blockPicker.addView) {
        if (localButton(controlsRect, LocalizeString::get("client.ui.clickGui.blockPicker.add.name").value(), true)) {
            blockPicker.addView = true;
            blockPicker.scroll = 0.f;
            blockPicker.lerpScroll = 0.f;
            blockPicker.editIndex = -1;
            blockSearchBox.reset();
            blockSearchBox.setSelected(true);
            mod->rebuildCatalog();
        }

        dc.ctx->PushAxisAlignedClip(listArea, D2D1_ANTIALIAS_MODE_ALIASED);

        float y = listArea.top - blockPicker.lerpScroll;
        float entryTextSize = rowH * 0.34f;
        int removeIdx = -1;

        if (entries.empty()) {
            dc.drawText(listArea, LocalizeString::get("client.ui.clickGui.blockPicker.empty.name").value(),
                        d2d::Color(1.f, 1.f, 1.f, 0.45f), FontSelection::PrimaryRegular, entryTextSize * 1.1f,
                        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        bool inList = listArea.contains(cursorPos);

        for (size_t i = 0; i < entries.size(); i++) {
            auto& e = *entries[i];
            RectF rowRect = { listArea.left, y, listArea.right, y + rowH };
            bool rowVisible = rowRect.bottom > listArea.top && rowRect.top < listArea.bottom;

            if (rowVisible) {
                bool rowHovered = inList && rowRect.contains(cursorPos);
                dc.fillRoundedRectangle(rowRect,
                                        d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(rowHovered ? 0.08f : 0.045f),
                                        rowH * 0.18f);

                float iconSize = rowH * 0.8f;
                RectF iconRect = { rowRect.left + rowH * 0.1f, y + rowH * 0.1f,
                                   rowRect.left + rowH * 0.1f + iconSize, y + rowH * 0.1f + iconSize };
                dc.fillRoundedRectangle(iconRect, d2d::Color(0.f, 0.f, 0.f, 0.35f), iconSize * 0.15f);
                pushIcon(iconRect, mod->findIconSource(e.id).block);

                float btnW = w * 0.14f;
                float btnH = rowH * 0.62f;
                float btnPad = rowH * 0.19f;
                RectF removeRect = { rowRect.right - btnPad - btnW, y + btnPad, rowRect.right - btnPad,
                                     y + btnPad + btnH };
                RectF editRect = { removeRect.left - btnPad - btnW, y + btnPad, removeRect.left - btnPad,
                                   y + btnPad + btnH };

                RectF nameRect = { iconRect.right + rowH * 0.25f, y + rowH * 0.08f, editRect.left - rowH * 0.2f,
                                   y + rowH * 0.52f };
                RectF idRect = { nameRect.left, y + rowH * 0.5f, nameRect.right, y + rowH * 0.92f };
                dc.drawSingleLineFitted(nameRect, e.displayName, { 1.f, 1.f, 1.f, 1.f },
                                        FontSelection::PrimaryRegular, entryTextSize, DWRITE_TEXT_ALIGNMENT_LEADING,
                                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                dc.drawSingleLineFitted(idRect, util::StrToWStr(e.id), d2d::Color(1.f, 1.f, 1.f, 0.45f),
                                        FontSelection::PrimaryRegular, entryTextSize * 0.82f,
                                        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                bool editHov = inList && editRect.contains(cursorPos);
                bool remHov = inList && removeRect.contains(cursorPos);
                dc.fillRoundedRectangle(editRect,
                                        editHov ? accentColor : d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.11f),
                                        btnH * 0.25f);
                dc.fillRoundedRectangle(removeRect,
                                        remHov ? d2d::Color(0.75f, 0.2f, 0.2f, 1.f)
                                               : d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.11f),
                                        btnH * 0.25f);
                dc.drawSingleLineFitted(editRect, LocalizeString::get("client.ui.clickGui.blockPicker.edit.name").value(),
                                        { 1.f, 1.f, 1.f, 1.f }, FontSelection::PrimaryRegular, btnH * 0.5f,
                                        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                dc.drawSingleLineFitted(removeRect,
                                        LocalizeString::get("client.ui.clickGui.blockPicker.remove.name").value(),
                                        { 1.f, 1.f, 1.f, 1.f }, FontSelection::PrimaryRegular, btnH * 0.5f,
                                        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                if (justClicked[0] && editHov) {
                    blockPicker.editIndex = blockPicker.editIndex == static_cast<int>(i) ? -1 : static_cast<int>(i);
                    playClickSound();
                }
                if (justClicked[0] && remHov) {
                    removeIdx = static_cast<int>(i);
                    playClickSound();
                }
            }
            y += rowH + rowGap;

            if (blockPicker.editIndex == static_cast<int>(i)) {
                float innerLeft = listArea.left + rowH * 0.35f;
                float innerW = listArea.getWidth() - rowH * 0.7f;
                y = drawSetting(e.colorSetting.get(), mod->settings.get(), { innerLeft, y }, dc, innerW, 0.25f, true) +
                    rowH * 0.25f;
                y = drawSetting(e.thicknessSetting.get(), mod->settings.get(), { innerLeft, y }, dc, innerW, 0.25f,
                                true) +
                    rowH * 0.35f;
            }
        }
        contentH = (y + blockPicker.lerpScroll) - listArea.top;
        dc.ctx->PopAxisAlignedClip();

        if (removeIdx >= 0) {
            auto& e = *entries[removeIdx];
            if (colorPicker.setting == e.colorSetting.get() || colorPicker.setting == e.thicknessSetting.get()) {
                colorPicker = ColorPicker();
            }
            if (activeSetting == e.colorSetting.get() || activeSetting == e.thicknessSetting.get()) {
                activeSetting = nullptr;
            }
            if (blockPicker.editIndex == removeIdx) {
                blockPicker.editIndex = -1;
            } else if (blockPicker.editIndex > removeIdx) {
                blockPicker.editIndex--;
            }
            mod->removeBlock(static_cast<size_t>(removeIdx));
        }
    } else {
        float backW = rowH;
        RectF backRect = { controlsRect.left, controlsRect.top, controlsRect.left + backW, controlsRect.bottom };
        bool backHov = backRect.contains(cursorPos);
        dc.fillRoundedRectangle(backRect, d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(backHov ? 0.2f : 0.11f),
                                rowH * 0.2f);
        RectF backIconRect = { backRect.left + backW * 0.22f, backRect.top + backW * 0.22f,
                               backRect.right - backW * 0.22f, backRect.bottom - backW * 0.22f };
        dc.ctx->DrawBitmap(Necromancer::getAssets().arrowBackIcon.getBitmap(), backIconRect);
        if (justClicked[0] && backHov) {
            blockPicker.addView = false;
            blockPicker.scroll = 0.f;
            blockPicker.lerpScroll = 0.f;
            blockSearchBox.setSelected(false);
            playClickSound();
        }

        RectF searchRect = { backRect.right + pad * 0.6f, controlsRect.top, controlsRect.right, controlsRect.bottom };
        dc.fillRoundedRectangle(searchRect, d2d::Color::RGB(0x70, 0x70, 0x70).asAlpha(0.28f),
                                searchRect.getHeight() * 0.35f);
        if (justClicked[0] && !backHov) {
            blockSearchBox.setSelected(searchRect.contains(cursorPos));
        }
        std::wstring searchText = blockSearchBox.getText();
        std::wstring searchDisplay =
            searchText.empty() && !blockSearchBox.isSelected()
                ? LocalizeString::get("client.ui.clickGui.blockPicker.search.name").value()
                : searchText;
        RectF searchTextRect = { searchRect.left + searchRect.getHeight() * 0.4f, searchRect.top,
                                 searchRect.right - 5.f, searchRect.bottom };
        Vec2 ts = dc.getTextSize(searchText.substr(0, blockSearchBox.getCaretLocation()),
                                 FontSelection::PrimaryRegular, searchRect.getHeight() / 2.f);
        float blinkerX = searchTextRect.left + ts.x;
        if (blockSearchBox.isSelected() && blockSearchBox.shouldBlink()) {
            dc.fillRectangle({ blinkerX, searchRect.top + 3.f, blinkerX + 2.f, searchRect.bottom - 3.f },
                             d2d::Color::RGB(0xB9, 0xB9, 0xB9));
        }
        dc.drawSingleLineFitted(searchTextRect, searchDisplay, d2d::Color::RGB(0xB9, 0xB9, 0xB9),
                                FontSelection::PrimaryRegular, searchRect.getHeight() / 2.f,
                                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        auto const& catalog = mod->getCatalog();
        std::wstring filter = lowercase(searchText);

        dc.ctx->PushAxisAlignedClip(listArea, D2D1_ANTIALIAS_MODE_ALIASED);

        if (catalog.empty()) {
            dc.drawText(listArea, LocalizeString::get("client.ui.clickGui.blockPicker.noWorld.name").value(),
                        d2d::Color(1.f, 1.f, 1.f, 0.45f), FontSelection::PrimaryRegular, rowH * 0.38f,
                        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        constexpr int cols = 3;
        float cellGap = pad * 0.7f;
        float cellW = (listArea.getWidth() - cellGap * (cols - 1)) / static_cast<float>(cols);
        float nameH = cellW * 0.3f;
        float cellH = cellW * 0.72f + nameH;
        float gridTextSize = nameH * 0.42f;
        bool inGrid = listArea.contains(cursorPos);

        int shown = 0;
        for (auto& cat : catalog) {
            if (!filter.empty() && cat.searchKey.find(filter) == std::wstring::npos) continue;
            int col = shown % cols;
            int gridRow = shown / cols;
            shown++;

            float cx = listArea.left + static_cast<float>(col) * (cellW + cellGap);
            float cy = listArea.top - blockPicker.lerpScroll + static_cast<float>(gridRow) * (cellH + cellGap);
            RectF cell { cx, cy, cx + cellW, cy + cellH };
            if (cell.bottom < listArea.top || cell.top > listArea.bottom) continue;

            bool added = mod->hasBlock(cat.id);
            bool hov = inGrid && cell.contains(cursorPos);
            dc.fillRoundedRectangle(cell, d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(hov && !added ? 0.14f : 0.05f),
                                    cellW * 0.08f);

            float iconSize = cellW * 0.52f;
            RectF iconRect { cell.centerX() - iconSize * 0.5f, cell.top + cellW * 0.08f,
                             cell.centerX() + iconSize * 0.5f, cell.top + cellW * 0.08f + iconSize };
            dc.fillRoundedRectangle(iconRect, d2d::Color(0.f, 0.f, 0.f, 0.35f), iconSize * 0.12f);
            pushIcon(iconRect, cat.block);

            RectF nameRect { cell.left + cellW * 0.06f, iconRect.bottom + cellW * 0.03f, cell.right - cellW * 0.06f,
                             cell.bottom - cellW * 0.03f };
            dc.drawWrappedTextClipped(nameRect, cat.displayName,
                                      added ? d2d::Color(1.f, 1.f, 1.f, 0.35f) : d2d::Color(1.f, 1.f, 1.f, 0.95f),
                                      FontSelection::PrimaryRegular, gridTextSize);

            if (added) {
                RectF checkRect { cell.right - cellW * 0.22f, cell.top + cellW * 0.06f, cell.right - cellW * 0.06f,
                                  cell.top + cellW * 0.22f };
                dc.ctx->DrawBitmap(Necromancer::getAssets().checkmarkIcon.getBitmap(), checkRect);
                if (hov) setTooltip(LocalizeString::get("client.ui.clickGui.blockPicker.added.name").value());
            } else if (hov && justClicked[0]) {
                mod->addBlock(cat);
                playClickSound();
                blockPicker.addView = false;
                blockPicker.scroll = 0.f;
                blockPicker.lerpScroll = 0.f;
                blockPicker.editIndex = static_cast<int>(mod->getEntries().size()) - 1;
                blockSearchBox.setSelected(false);
                break;
            }
        }
        int totalRows = (shown + cols - 1) / cols;
        contentH = static_cast<float>(totalRows) * (cellH + cellGap);
        dc.ctx->PopAxisAlignedClip();
    }

    blockPicker.scrollMax = std::max(0.f, contentH - listArea.getHeight());
    blockPicker.scroll = std::clamp(blockPicker.scroll, 0.f, blockPicker.scrollMax);
    blockPicker.lerpScroll =
        std::lerp(blockPicker.lerpScroll, blockPicker.scroll, Necromancer::getRenderer().getDeltaTime() / 5.f);

    RectF dragBar = { bPickerRect.left, bPickerRect.top, bPickerRect.right, titleRect.bottom };
    if (!blockPicker.dragging && justClicked[0] && dragBar.contains(cursorPos) && !xRect.contains(cursorPos)) {
        blockPicker.dragging = true;
        blockPicker.dragOffs = cursorPos - bPickerRect.getPos();
    }
    if (!mouseButtons[0]) blockPicker.dragging = false;
    if (blockPicker.dragging) {
        bPickerRect.setPos(cursorPos - blockPicker.dragOffs);
    }
    auto ss = Necromancer::getRenderer().getScreenSize();
    util::KeepInBounds(bPickerRect, { 0.f, 0.f, ss.width, ss.height });

    if (calcAnim > 0.995f) {
        auto* frameBmp = Necromancer::getRenderer().getCopiedBitmap();
        if (frameBmp) {
            for (auto& ic : icons) {
                auto rcf = ic.rect.get();
                dc.ctx->DrawBitmap(frameBmp, &rcf, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &rcf);
            }
        }
        mod->setIconDraws(std::move(icons));
    } else {
        mod->clearIconDraws();
    }

    justClicked[0] = savedClick;
}

void ClickGUI::onEnable(bool ignoreAnims) {
    calcAnim = 0.f;
    if (ignoreAnims) calcAnim = 1.f;
    scroll = 0.f;
    lerpScroll = 0.f;
    mouseButtons = {};
    justClicked = {};
    draggingScrollbar = false;
    dropdownSetting = nullptr;
    dropdownAnimations.clear();
    this->tab = MODULES;
}

void ClickGUI::onDisable() {
    capturedKey = 0;
    activeSetting = nullptr;
    dropdownSetting = nullptr;
    draggingScrollbar = false;
    dropdownAnimations.clear();
    searchTextBox.reset();
    searchTextBox.setSelected(false);
    configNameTextBox.setSelected(false);
    configInputOpen = false;

    closeBlockPicker();

    if (itemSwitcherPicker.mod) closeItemSwitcher();

    clearSettingBoxFocus();

    for (auto& tb : this->pickerTextBoxes) {
        tb.setSelected(false);
    }

    plRenameBox.setSelected(false);
    plRenamingTag.clear();
    plPriorityDrag.clear();

    Necromancer::getConfigManager().saveCurrentConfig();
}
