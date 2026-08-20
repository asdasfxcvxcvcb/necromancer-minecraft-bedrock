#include "pch.h"
#include "ClickGUI.h"
#include "client/Necromancer.h"
#include "client/config/ConfigManager.h"
#include "client/feature/module/ModuleManager.h"
#include "client/feature/module/modules/visual/BlockESP.h"
#include "client/misc/ConditionGraph.h"
#include "client/misc/ItemCatalog.h"
#include "client/misc/KeybindManager.h"
#include "client/render/Renderer.h"
#include "../../render/asset/Assets.h"
#include "util/DrawContext.h"
#include <algorithm>
#include <cwctype>
#include <functional>
#include <iomanip>

using FontSelection = Renderer::FontSelection;

namespace {
    constexpr CondKind paletteKinds[] = {
        CondKind::And,
        CondKind::Or,
        CondKind::Not,
        CondKind::Wait,
        CondKind::HoldingItem,
        CondKind::Health,
        CondKind::Hunger,
        CondKind::FallDistance,
        CondKind::RiseDistance,
        CondKind::SwitchToItem,
        CondKind::TookDamage,
        CondKind::DealtDamage,
        CondKind::EnemiesInRange,
        CondKind::Sprinting,
        CondKind::Walking,
        CondKind::Sneaking,
        CondKind::OnGround,
    };

    std::wstring kindLabel(CondKind kind) {
        std::string key = std::string("client.ui.clickGui.keybinds.cond.") + ConditionGraph::kindKey(kind) + ".name";
        auto text = LocalizeString::get(key).value();
        if (text.empty() || text == util::StrToWStr(key)) return util::StrToWStr(ConditionGraph::kindKey(kind));
        return text;
    }

    std::wstring compareSymbol(CondCompare cmp) {
        switch (cmp) {
        case CondCompare::Less:
            return L"<";
        case CondCompare::LessEqual:
            return L"\x2264";
        case CondCompare::Greater:
            return L">";
        default:
            return L"\x2265";
        }
    }

    std::wstring formatNumber(float value) {
        std::wstringstream ss;
        if (std::fabs(value - std::round(value)) < 0.01f) ss << static_cast<int>(std::round(value));
        else ss << std::fixed << std::setprecision(1) << value;
        return ss.str();
    }

    std::wstring nodeSummary(CondNode const& node) {
        std::wstring base = kindLabel(node.kind);
        switch (node.kind) {
        case CondKind::Health:
        case CondKind::Hunger:
        case CondKind::FallDistance:
        case CondKind::RiseDistance: {
            std::wstring value = formatNumber(node.threshold);
            if (node.kind == CondKind::Health && node.percent) value += L"%";
            return base + L" " + compareSymbol(node.compare) + L" " + value;
        }
        case CondKind::EnemiesInRange:
            return base + L" \x2265" + std::to_wstring(node.count) + L" @" + formatNumber(node.range) + L"m";
        case CondKind::TookDamage:
        case CondKind::DealtDamage:
            return base + L" " + formatNumber(node.windowMs / 1000.f) + L"s";
        case CondKind::Wait:
            return base + L" " + formatNumber(node.waitMs) + L" ms";
        case CondKind::HoldingItem:
        case CondKind::SwitchToItem: {
            if (node.itemId.empty()) return base;
            return ItemCatalog::get().displayNameFor(node.itemId);
        }
        default:
            return base;
        }
    }

    d2d::Color kindColor(CondKind kind) {
        if (kind == CondKind::Wait) return d2d::Color::RGB(0xF0, 0xB4, 0x29);
        if (ConditionGraph::isLogic(kind)) return d2d::Color::RGB(0x5C, 0x7C, 0xFA);
        switch (kind) {
        case CondKind::HoldingItem:
        case CondKind::SwitchToItem:
            return d2d::Color::RGB(0xE8, 0x9C, 0x3A);
        case CondKind::Health:
        case CondKind::Hunger:
            return d2d::Color::RGB(0xE0, 0x53, 0x53);
        case CondKind::TookDamage:
        case CondKind::DealtDamage:
            return d2d::Color::RGB(0xC9, 0x4F, 0x8E);
        case CondKind::EnemiesInRange:
            return d2d::Color::RGB(0x8E, 0x5C, 0xD0);
        default:
            return d2d::Color::RGB(0x3F, 0xA9, 0x6A);
        }
    }

    struct ScratchMetrics {
        float notchX;
        float notchW;
        float notchD;
        float corner;
    };

    void addTopEdge(ID2D1GeometrySink* sink, float left, float right, float top, ScratchMetrics const& m, bool notch) {
        sink->AddLine(D2D1::Point2F(left + m.corner, top));
        if (notch) {
            float nx = left + m.notchX;
            sink->AddLine(D2D1::Point2F(nx, top));
            sink->AddLine(D2D1::Point2F(nx + m.notchD, top + m.notchD));
            sink->AddLine(D2D1::Point2F(nx + m.notchW - m.notchD, top + m.notchD));
            sink->AddLine(D2D1::Point2F(nx + m.notchW, top));
        }
        sink->AddLine(D2D1::Point2F(right - m.corner, top));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(right, top + m.corner), D2D1::SizeF(m.corner, m.corner), 0.f,
                                      D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    }

    void addBottomEdge(ID2D1GeometrySink* sink, float left, float right, float bottom, ScratchMetrics const& m,
                       bool bump) {
        sink->AddLine(D2D1::Point2F(right, bottom - m.corner));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(right - m.corner, bottom), D2D1::SizeF(m.corner, m.corner), 0.f,
                                      D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        if (bump) {
            float nx = left + m.notchX;
            sink->AddLine(D2D1::Point2F(nx + m.notchW, bottom));
            sink->AddLine(D2D1::Point2F(nx + m.notchW - m.notchD, bottom + m.notchD));
            sink->AddLine(D2D1::Point2F(nx + m.notchD, bottom + m.notchD));
            sink->AddLine(D2D1::Point2F(nx, bottom));
        }
        sink->AddLine(D2D1::Point2F(left + m.corner, bottom));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(left, bottom - m.corner), D2D1::SizeF(m.corner, m.corner), 0.f,
                                      D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    }

    // Simple stacking statement block: notch on top, bump on bottom.
    void fillStatementBlock(D2DUtil& dc, d2d::Rect const& rc, d2d::Color const& col, ScratchMetrics const& m,
                            bool notch, bool bump) {
        auto* factory = Necromancer::getRenderer().getFactory();
        if (!factory) return;
        ComPtr<ID2D1PathGeometry> geo;
        if (FAILED(factory->CreatePathGeometry(geo.GetAddressOf()))) return;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(geo->Open(sink.GetAddressOf()))) return;

        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        sink->BeginFigure(D2D1::Point2F(rc.left, rc.top + m.corner), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(rc.left + m.corner, rc.top), D2D1::SizeF(m.corner, m.corner), 0.f,
                                      D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        addTopEdge(sink.Get(), rc.left, rc.right, rc.top, m, notch);
        sink->AddLine(D2D1::Point2F(rc.right, rc.bottom - m.corner));
        addBottomEdge(sink.Get(), rc.left, rc.right, rc.bottom, m, bump);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (FAILED(sink->Close())) return;

        dc.brush->SetColor(col.get());
        dc.ctx->FillGeometry(geo.Get(), dc.brush);
    }

    // C-shaped container: header bar (top notch + bump into mouth), left spine, bottom bar (bump out).
    void fillCBlock(D2DUtil& dc, float left, float right, float top, float bottom, float headerH, float footerH,
                    float armW, d2d::Color const& col, ScratchMetrics const& m, bool notch, bool bump) {
        auto* factory = Necromancer::getRenderer().getFactory();
        if (!factory) return;
        ComPtr<ID2D1PathGeometry> geo;
        if (FAILED(factory->CreatePathGeometry(geo.GetAddressOf()))) return;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(geo->Open(sink.GetAddressOf()))) return;

        float mouthTop = top + headerH;
        float mouthBottom = bottom - footerH;
        float innerLeft = left + armW;

        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        sink->BeginFigure(D2D1::Point2F(left, top + m.corner), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(left + m.corner, top), D2D1::SizeF(m.corner, m.corner), 0.f,
                                      D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        addTopEdge(sink.Get(), left, right, top, m, notch);
        // down right side of header
        sink->AddLine(D2D1::Point2F(right, mouthTop - m.corner));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(right - m.corner, mouthTop), D2D1::SizeF(m.corner, m.corner), 0.f,
                                      D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        // header inner edge back to spine (bump pointing into the mouth)
        float nx = innerLeft + m.notchX;
        sink->AddLine(D2D1::Point2F(nx + m.notchW, mouthTop));
        sink->AddLine(D2D1::Point2F(nx + m.notchW - m.notchD, mouthTop + m.notchD));
        sink->AddLine(D2D1::Point2F(nx + m.notchD, mouthTop + m.notchD));
        sink->AddLine(D2D1::Point2F(nx, mouthTop));
        sink->AddLine(D2D1::Point2F(innerLeft + m.corner, mouthTop));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(innerLeft, mouthTop + m.corner), D2D1::SizeF(m.corner, m.corner),
                                      0.f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        // down the spine
        sink->AddLine(D2D1::Point2F(innerLeft, mouthBottom - m.corner));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(innerLeft + m.corner, mouthBottom), D2D1::SizeF(m.corner, m.corner),
                                      0.f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        // footer inner edge out to right
        sink->AddLine(D2D1::Point2F(right - m.corner, mouthBottom));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(right, mouthBottom + m.corner), D2D1::SizeF(m.corner, m.corner),
                                      0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        // down right of footer
        sink->AddLine(D2D1::Point2F(right, bottom - m.corner));
        addBottomEdge(sink.Get(), left, right, bottom, m, bump);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (FAILED(sink->Close())) return;

        dc.brush->SetColor(col.get());
        dc.ctx->FillGeometry(geo.Get(), dc.brush);
    }
}

void ClickGUI::openCondCanvas(std::string const& bindName) {
    condCanvas = CondCanvas {};
    condCanvas.bind = bindName;
    condCanvas.open = true;

    if (!condItemSearchRegistered) {
        condItemSearchRegistered = true;
        Necromancer::get().addTextBox(&condItemSearchBox);
    }
    if (!condWaitMsRegistered) {
        condWaitMsRegistered = true;
        Necromancer::get().addTextBox(&condWaitMsBox);
    }
    if (!condThresholdRegistered) {
        condThresholdRegistered = true;
        Necromancer::get().addTextBox(&condThresholdBox);
    }
    if (!condCountRegistered) {
        condCountRegistered = true;
        Necromancer::get().addTextBox(&condCountBox);
    }
    if (!condRangeRegistered) {
        condRangeRegistered = true;
        Necromancer::get().addTextBox(&condRangeBox);
    }
    if (!condDamageWindowRegistered) {
        condDamageWindowRegistered = true;
        Necromancer::get().addTextBox(&condDamageWindowBox);
    }
    condItemSearchBox.reset();
    condItemSearchBox.setSelected(false);
    condWaitMsBox.reset();
    condWaitMsBox.setSelected(false);
    condWaitMsNode = 0;

    kbCapturingKey.clear();
    kbRenameBox.setSelected(false);
    kbRenamingBind.clear();
    kbEditingBind.clear();
}

void ClickGUI::closeCondCanvas() {
    if (!condCanvas.open) return;
    condItemSearchBox.setSelected(false);
    condItemSearchBox.reset();
    condWaitMsBox.setSelected(false);
    condWaitMsBox.reset();
    condWaitMsNode = 0;
    condThresholdBox.setSelected(false);
    condThresholdBox.reset();
    condThresholdNode = 0;
    condCountBox.setSelected(false);
    condCountBox.reset();
    condCountNode = 0;
    condRangeBox.setSelected(false);
    condRangeBox.reset();
    condRangeNode = 0;
    condDamageWindowBox.setSelected(false);
    condDamageWindowBox.reset();
    condDamageWindowNode = 0;
    condCanvas = CondCanvas {};
    condCanvasRect = {};
    condBoardRect = {};
    condPaletteRect = {};
    condItemPickerRect = {};
    condIconDraws.clear();
    Necromancer::getConfigManager().saveCurrentConfig();
}

void ClickGUI::drawCondCanvas(D2DUtil& dc, bool rtl) {
    if (!condCanvas.open) return;

    auto& mgr = KeybindManager::get();
    auto* bind = mgr.findBind(condCanvas.bind);
    if (!bind || bind->kind != KeybindManager::KindIf) {
        condCanvas.queueClose = true;
        return;
    }

    auto& graph = bind->graph;
    auto& cursorPos = SDK::ClientInstance::get()->cursorPos;
    auto accentColor = d2d::Color(Necromancer::get().getAccentColor().getMainColor());

    bool savedClick = justClicked[0];
    if (condCanvas.itemPickerNode != 0) justClicked[0] = false;

    clearLayers();

    float pad = rect.getHeight() * 0.018f;
    condCanvasRect = { rect.left + pad, rect.top + pad, rect.right - pad, rect.bottom - pad };

    dc.fillRoundedRectangle(condCanvasRect, d2d::Color::RGB(0x0A, 0x0A, 0x0A).asAlpha(0.97f), 14.f * adaptedScale);
    dc.drawRoundedRectangle(condCanvasRect, accentColor.asAlpha(0.75f), 14.f * adaptedScale, 2.f * adaptedScale,
                            DrawUtil::OutlinePosition::Inside);

    float innerPad = pad * 0.8f;
    float titleH = rect.getHeight() * 0.045f;
    d2d::Rect titleRect { condCanvasRect.left + innerPad, condCanvasRect.top + innerPad * 0.6f,
                          condCanvasRect.right - innerPad - titleH * 3.4f, condCanvasRect.top + innerPad * 0.6f + titleH };

    std::wstring title = LocalizeString::get("client.ui.clickGui.keybinds.cond.title.name").value() + L" \x2014 " +
        util::StrToWStr(bind->name);
    dc.drawAutoFitted(titleRect, title, d2d::Colors::WHITE, FontSelection::PrimaryLight, titleH * 0.6f,
                            rtl ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING,
                            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    float btnH = titleH * 0.82f;
    d2d::Rect resetRc { titleRect.right + innerPad * 0.4f, titleRect.centerY() - btnH * 0.5f,
                        titleRect.right + innerPad * 0.4f + titleH * 1.7f, titleRect.centerY() + btnH * 0.5f };
    if (drawButton(dc, resetRc, LocalizeString::get("client.ui.clickGui.keybinds.cond.recenter.name").value(),
                   LocalizeString::get("client.ui.clickGui.keybinds.cond.recenter.desc").value(), true)) {
        condCanvas.boardScroll = 0.f;
        playClickSound();
    }

    d2d::Rect closeRc { condCanvasRect.right - innerPad - btnH, titleRect.centerY() - btnH * 0.5f,
                        condCanvasRect.right - innerPad, titleRect.centerY() + btnH * 0.5f };
    if (auto* bmp = Necromancer::getAssets().xIcon.getBitmap()) dc.ctx->DrawBitmap(bmp, closeRc.get());
    if (shouldSelect(closeRc, cursorPos)) {
        setTooltip(LocalizeString::get("client.ui.clickGui.keybinds.cond.close.desc"));
        if (justClicked[0]) {
            condCanvas.queueClose = true;
            playClickSound();
        }
    }

    float paletteW = condCanvasRect.getWidth() * 0.19f;
    float contentTop = titleRect.bottom + innerPad * 0.5f;

    if (rtl) {
        condPaletteRect = { condCanvasRect.left + innerPad, contentTop, condCanvasRect.left + innerPad + paletteW,
                            condCanvasRect.bottom - innerPad };
        condBoardRect = { condPaletteRect.right + innerPad * 0.7f, contentTop, condCanvasRect.right - innerPad,
                          condCanvasRect.bottom - innerPad };
    } else {
        condPaletteRect = { condCanvasRect.right - innerPad - paletteW, contentTop, condCanvasRect.right - innerPad,
                            condCanvasRect.bottom - innerPad };
        condBoardRect = { condCanvasRect.left + innerPad, contentTop, condPaletteRect.left - innerPad * 0.7f,
                          condCanvasRect.bottom - innerPad };
    }

    dc.fillRoundedRectangle(condBoardRect, d2d::Color::RGB(0x13, 0x13, 0x13).asAlpha(0.95f), 10.f * adaptedScale);
    dc.drawRoundedRectangle(condBoardRect, d2d::Color(1.f, 1.f, 1.f, 0.12f), 10.f * adaptedScale, 1.f,
                            DrawUtil::OutlinePosition::Inside);

    bool overBoard = condBoardRect.contains(cursorPos);

    dc.ctx->PushAxisAlignedClip(condBoardRect.get(), D2D1_ANTIALIAS_MODE_ALIASED);

    float gridStep = 28.f * adaptedScale;
    if (gridStep > 4.f) {
        auto gridCol = d2d::Color(1.f, 1.f, 1.f, 0.04f);
        for (float x = condBoardRect.left; x < condBoardRect.right; x += gridStep)
            dc.fillRectangle({ x, condBoardRect.top, x + 1.f, condBoardRect.bottom }, gridCol);
        for (float y = condBoardRect.top; y < condBoardRect.bottom; y += gridStep)
            dc.fillRectangle({ condBoardRect.left, y, condBoardRect.right, y + 1.f }, gridCol);
    }

    float bpad   = condBoardRect.getWidth() * 0.03f;
    float rowH   = rect.getHeight() * 0.064f;
    float armW   = rowH * 0.42f;
    float gap    = rowH * 0.06f;
    float textSz = rowH * 0.47f;
    float addH   = rowH * 0.86f;

    ScratchMetrics metrics {
        rowH * 0.55f,   // notchX  (offset from left where notch starts)
        rowH * 0.9f,    // notchW  (notch width)
        rowH * 0.16f,   // notchD  (notch depth)
        rowH * 0.16f    // corner
    };

    int clickedNode  = 0;
    int addTargetId  = 0;
    float totalH     = 0.f;

    bool isDragging = condCanvas.dragging;
    int draggedId = condCanvas.dragNode;

    // while dragging an existing node, it and its whole subtree are hidden from the stack
    std::function<bool(int)> insideDragged = [&](int id) -> bool {
        if (!isDragging || draggedId == 0) return false;
        int cur = id;
        for (int guard = 0; guard < ConditionGraph::maxNodes && cur != 0; guard++) {
            if (cur == draggedId) return true;
            cur = graph.parentOf(cur);
        }
        return false;
    };

    struct DropSlot {
        int parent;
        int index;
        d2d::Rect rc;
    };
    std::vector<DropSlot> dropSlots;
    dropSlots.reserve(32);

    std::function<float(int)> measureH = [&](int id) -> float {
        auto const* n = graph.find(id);
        if (!n || insideDragged(id)) return 0.f;
        if (!ConditionGraph::isLogic(n->kind)) return rowH + gap;
        float h = rowH;                 // header bar
        for (int child : n->inputs)
            h += measureH(child);
        h += addH + gap;                // + add row inside mouth
        h += rowH * 0.5f;               // footer bar
        return h + gap;
    };

    std::function<float(int, float, float)> drawBlock = [&](int id, float x, float y) -> float {
        auto* n = graph.find(id);
        if (!n || insideDragged(id)) return 0.f;

        bool selected = id == condCanvas.selectedNode;
        bool isLogic  = ConditionGraph::isLogic(n->kind);
        auto baseCol  = kindColor(n->kind);

        float right = condBoardRect.right - bpad;

        if (!isLogic) {
            d2d::Rect rc { x, y, right, y + rowH };
            bool hovered = overBoard && !isDragging && rc.contains(cursorPos);
            fillStatementBlock(dc, rc, baseCol.asAlpha(hovered || selected ? 1.f : 0.9f), metrics, true, true);
            if (selected)
                dc.drawRoundedRectangle(rc, d2d::Colors::WHITE, metrics.corner, std::max(1.f, 2.f * adaptedScale),
                                        DrawUtil::OutlinePosition::Inside);

            float tx = rc.left + rowH * 0.35f;
            if (n->kind == CondKind::HoldingItem && !n->itemId.empty()) {
                float iconSz = rowH * 0.66f;
                d2d::Rect iconRc { tx, rc.top + (rowH - iconSz) * 0.5f, tx + iconSz, rc.top + (rowH + iconSz) * 0.5f };
                if (void* block = ItemCatalog::get().blockFor(n->itemId))
                    condIconDraws.emplace_back(iconRc, block);
                tx = iconRc.right + rowH * 0.18f;
            }
            std::wstring label = nodeSummary(*n);
            if (n->invert) label = L"! " + label;
            dc.drawAutoFitted({ tx, rc.top, right - rowH * 0.3f, rc.bottom }, label, d2d::Colors::WHITE,
                                    FontSelection::PrimaryRegular, textSz, DWRITE_TEXT_ALIGNMENT_LEADING,
                                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (overBoard && justClicked[0] && rc.contains(cursorPos)) clickedNode = id;
            return rowH + gap;
        }

        // C-block: measure children first so we know total height
        float bodyH = 0.f;
        for (int child : n->inputs) bodyH += measureH(child);
        float mouthH = bodyH + addH + gap;
        float footerH = rowH * 0.5f;
        float total = rowH + mouthH + footerH;

        d2d::Rect headRc { x, y, right, y + rowH };
        bool hovered = overBoard && !isDragging && headRc.contains(cursorPos);
        fillCBlock(dc, x, right, y, y + total, rowH, footerH, armW,
                   baseCol.asAlpha(hovered || selected ? 1.f : 0.9f), metrics, true, true);
        if (selected)
            dc.drawRoundedRectangle(headRc, d2d::Colors::WHITE, metrics.corner,
                                    std::max(1.f, 2.f * adaptedScale), DrawUtil::OutlinePosition::Inside);

        std::wstring label = nodeSummary(*n);
        if (n->invert) label = L"! " + label;
        dc.drawAutoFitted({ x + rowH * 0.35f, y, right - rowH * 0.3f, y + rowH }, label, d2d::Colors::WHITE,
                                FontSelection::PrimarySemilight, textSz, DWRITE_TEXT_ALIGNMENT_LEADING,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (overBoard && justClicked[0] && headRc.contains(cursorPos)) clickedNode = id;

        float childX = x + armW;
        float cy = y + rowH;
        int slotIndex = 0;
        for (int child : n->inputs) {
            if (insideDragged(child)) continue;
            dropSlots.push_back({ id, slotIndex, { childX, cy - gap * 0.5f, right, cy + gap * 0.5f } });
            cy += drawBlock(child, childX, cy);
            slotIndex++;
        }

        d2d::Rect addRc { childX, cy, right, cy + addH };
        dropSlots.push_back({ id, slotIndex, addRc });

        bool addHov = overBoard && !isDragging && addRc.contains(cursorPos);
        dc.fillRoundedRectangle(addRc, d2d::Color(1.f, 1.f, 1.f, addHov ? 0.2f : 0.1f), metrics.corner);
        dc.drawAutoFitted(addRc, isDragging ? L"drop here" : L"+ add", d2d::Color(1.f, 1.f, 1.f, 0.7f),
                                FontSelection::PrimaryRegular, addH * 0.44f, DWRITE_TEXT_ALIGNMENT_CENTER,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (addHov && justClicked[0]) addTargetId = id;

        return total + gap;
    };

    auto topLevels = graph.topLevelIds();
    std::vector<int> visibleTopLevels;
    visibleTopLevels.reserve(topLevels.size());
    for (int id : topLevels) {
        if (!insideDragged(id)) visibleTopLevels.push_back(id);
    }

    if (graph.empty() || visibleTopLevels.empty()) {
        condCanvas.boardScroll = 0.f;
        condCanvas.boardScrollMax = 0.f;
        dc.drawAutoFitted(condBoardRect,
                                isDragging ? L"drop to place" : L"Grab a block from the panel \u2192",
                                d2d::Color(1.f, 1.f, 1.f, 0.28f), FontSelection::PrimaryLight,
                                rect.getHeight() * 0.028f, DWRITE_TEXT_ALIGNMENT_CENTER,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    } else {
        float startY = condBoardRect.top + bpad + metrics.corner - condCanvas.boardScroll;
        float cursorY = startY;
        float maxHeight = 0.f;

        for (size_t i = 0; i < visibleTopLevels.size(); i++) {
            int id = visibleTopLevels[i];
            if (i > 0) cursorY += gap;
            float blockH = drawBlock(id, condBoardRect.left + bpad, cursorY);
            cursorY += blockH;
            maxHeight = std::max(maxHeight, cursorY - startY);
        }

        totalH = maxHeight;
        condCanvas.boardScrollMax = std::max(0.f, totalH - condBoardRect.getHeight() + bpad * 2.f);
        condCanvas.boardScroll = std::clamp(condCanvas.boardScroll, 0.f, condCanvas.boardScrollMax);
    }
    // resolve which slot the drag is hovering, then show the insert marker
    condCanvas.dropValid = false;
    condCanvas.dropParent = 0;
    condCanvas.dropIndex = -1;

    if (isDragging && overBoard) {
        float bestDist = rowH * 2.2f;
        for (auto const& slot : dropSlots) {
            if (draggedId != 0) {
                if (slot.parent == draggedId) continue;
                if (graph.wouldCycle(slot.parent, draggedId)) continue;
            }
            float cx = std::clamp(cursorPos.x, slot.rc.left, slot.rc.right);
            float cy = std::clamp(cursorPos.y, slot.rc.top, slot.rc.bottom);
            float dx = cursorPos.x - cx;
            float dy = cursorPos.y - cy;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                condCanvas.dropParent = slot.parent;
                condCanvas.dropIndex = slot.index;
                condCanvas.dropRect = slot.rc;
                condCanvas.dropValid = true;
            }
        }

        if (condCanvas.dropValid) {
            d2d::Rect mark { condCanvas.dropRect.left, condCanvas.dropRect.top - rowH * 0.06f,
                             condCanvas.dropRect.right, condCanvas.dropRect.top + rowH * 0.06f };
            dc.fillRoundedRectangle(mark, accentColor, mark.getHeight() * 0.5f);
        }
    }

    dc.ctx->PopAxisAlignedClip();

    // press on a block: arm a possible drag (selection happens on release if we never moved)
    if (clickedNode != 0 && !condCanvas.dragging) {
        condCanvas.dragArmed = true;
        condCanvas.dragNode = clickedNode;
        condCanvas.dragNewKind = 0;
        condCanvas.dragStart = cursorPos;
    }

    if (addTargetId != 0 && !condCanvas.dragging) {
        condCanvas.selectedNode = addTargetId;
        playClickSound();
    }

    // armed -> dragging once the cursor moves far enough
    if (condCanvas.dragArmed && mouseButtons[0] && !condCanvas.dragging) {
        float dx = cursorPos.x - condCanvas.dragStart.x;
        float dy = cursorPos.y - condCanvas.dragStart.y;
        if (std::sqrt(dx * dx + dy * dy) > rowH * 0.35f) {
            condCanvas.dragging = true;
            if (condCanvas.dragNode != 0) condCanvas.selectedNode = condCanvas.dragNode;
        }
    }

    // release: either commit the drop, or treat it as a plain click-select
    if (!mouseButtons[0] && (condCanvas.dragArmed || condCanvas.dragging)) {
        if (condCanvas.dragging) {
            bool placed = false;

            if (condCanvas.dropValid && condCanvas.dropParent != 0) {
                int childId = condCanvas.dragNode;
                if (childId == 0 && condCanvas.dragNewKind != 0) {
                    childId = graph.addNode(static_cast<CondKind>(condCanvas.dragNewKind), 0.f, 0.f);
                }
                if (childId != 0 && graph.connectAt(condCanvas.dropParent, childId, condCanvas.dropIndex)) {
                    condCanvas.selectedNode = childId;
                    mgr.markDirty();
                    placed = true;
                }
            } else if (condCanvas.dragNewKind != 0 && overBoard && visibleTopLevels.empty()) {
                // dropping the very first block onto an empty board
                int newId = graph.addNode(static_cast<CondKind>(condCanvas.dragNewKind), 0.f, 0.f);
                if (newId != 0) {
                    condCanvas.selectedNode = newId;
                    mgr.markDirty();
                    placed = true;
                }
            }

            if (placed) playClickSound();
        } else if (condCanvas.dragNode != 0) {
            condCanvas.selectedNode =
                (condCanvas.selectedNode == condCanvas.dragNode) ? 0 : condCanvas.dragNode;
            playClickSound();
        } else if (condCanvas.dragNewKind != 0) {
            // palette row pressed and released without moving: plain click-to-add
            CondKind newKind = static_cast<CondKind>(condCanvas.dragNewKind);
            int newId = graph.addNode(newKind, 0.f, 0.f);
            if (newId != 0) {
                int oldRoot = graph.getRoot();
                if (ConditionGraph::maxArity(newKind) == 1 && oldRoot != 0 && oldRoot != newId) {
                    graph.connect(newId, oldRoot);
                } else if (graph.getRoot() != newId) {
                    int parentId = 0;
                    auto* sel = condCanvas.selectedNode != 0 ? graph.find(condCanvas.selectedNode) : nullptr;
                    if (sel && ConditionGraph::isLogic(sel->kind) && sel->id != newId) {
                        parentId = sel->id;
                    } else {
                        auto* rootN = graph.find(graph.getRoot());
                        if (rootN && ConditionGraph::isLogic(rootN->kind)) parentId = rootN->id;
                    }
                    if (parentId == 0 || !graph.connect(parentId, newId)) {
                        int rootBeforeAnd = graph.getRoot();
                        if (rootBeforeAnd != 0 && rootBeforeAnd != newId) {
                            int andId = graph.addNode(CondKind::And, 0.f, 0.f);
                            if (andId != 0) {
                                graph.connect(andId, rootBeforeAnd);
                                graph.connect(andId, newId);
                            }
                        }
                    }
                }
                condCanvas.selectedNode = newId;
                mgr.markDirty();
                playClickSound();
            }
        }

        condCanvas.dragging = false;
        condCanvas.dragArmed = false;
        condCanvas.dragNode = 0;
        condCanvas.dragNewKind = 0;
        condCanvas.dropValid = false;
        condCanvas.dropParent = 0;
        condCanvas.dropIndex = -1;
    }

    drawCondPalette(dc, *bind, graph);

    // floating ghost of whatever is being dragged, follows the cursor
    if (condCanvas.dragging) {
        CondKind ghostKind = CondKind::Invalid;
        std::wstring ghostLabel;
        bool ghostInvert = false;

        if (condCanvas.dragNode != 0) {
            if (auto const* dn = graph.find(condCanvas.dragNode)) {
                ghostKind = dn->kind;
                ghostLabel = nodeSummary(*dn);
                ghostInvert = dn->invert;
            }
        } else if (condCanvas.dragNewKind != 0) {
            ghostKind = static_cast<CondKind>(condCanvas.dragNewKind);
            ghostLabel = kindLabel(ghostKind);
        }

        if (ghostKind != CondKind::Invalid) {
            float gw = condBoardRect.getWidth() * 0.42f;
            d2d::Rect ghostRc { cursorPos.x - gw * 0.25f, cursorPos.y - rowH * 0.5f,
                                cursorPos.x - gw * 0.25f + gw, cursorPos.y + rowH * 0.5f };
            fillStatementBlock(dc, ghostRc, kindColor(ghostKind).asAlpha(0.75f), metrics, true, true);
            if (ghostInvert) ghostLabel = L"! " + ghostLabel;
            dc.drawAutoFitted({ ghostRc.left + rowH * 0.35f, ghostRc.top, ghostRc.right - rowH * 0.3f,
                                      ghostRc.bottom },
                                    ghostLabel, d2d::Color(1.f, 1.f, 1.f, 0.95f), FontSelection::PrimaryRegular,
                                    textSz, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    justClicked[0] = savedClick;

    if (condCanvas.itemPickerNode != 0) {
        addLayer(condItemPickerRect);
        drawCondItemPicker(dc);
    }

    flushCondIcons(dc);
}

void ClickGUI::flushCondIcons(D2DUtil& dc) {
    auto baseMod = Necromancer::getModuleManager().find("BlockESP");
    auto mod = baseMod ? std::static_pointer_cast<BlockESP>(baseMod) : nullptr;
    if (!mod) {
        condIconDraws.clear();
        return;
    }

    if (condIconDraws.empty()) {
        mod->clearIconDraws();
        condIconDraws.clear();
        return;
    }

    std::vector<BlockESP::IconDraw> draws;
    draws.reserve(condIconDraws.size());
    auto* frameBmp = Necromancer::getRenderer().getCopiedBitmap();

    for (auto& [iconRect, block] : condIconDraws) {
        if (!block) continue;
        d2d::Rect snapped { std::round(iconRect.left), std::round(iconRect.top), std::round(iconRect.right),
                            std::round(iconRect.bottom) };
        if (snapped.getWidth() < 1.f || snapped.getHeight() < 1.f) continue;
        if (frameBmp) {
            auto rcf = snapped.get();
            dc.ctx->DrawBitmap(frameBmp, &rcf, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &rcf);
        }
        draws.push_back({ snapped, block });
    }

    mod->setIconDraws(std::move(draws));
    condIconDraws.clear();
}

void ClickGUI::drawCondPalette(D2DUtil& dc, Keybind& bind, ConditionGraph& graph) {
    auto& mgr = KeybindManager::get();
    auto& cursorPos = SDK::ClientInstance::get()->cursorPos;
    auto accentColor = d2d::Color(Necromancer::get().getAccentColor().getMainColor());

    dc.fillRoundedRectangle(condPaletteRect, d2d::Color::RGB(0x16, 0x16, 0x16).asAlpha(0.95f), 10.f * adaptedScale);
    dc.drawRoundedRectangle(condPaletteRect, d2d::Color(1.f, 1.f, 1.f, 0.12f), 10.f * adaptedScale, 1.f,
                            DrawUtil::OutlinePosition::Inside);

    float pad = condPaletteRect.getWidth() * 0.06f;
    float rowH = rect.getHeight() * 0.058f;
    float rowGap = rowH * 0.16f;

    d2d::Rect header { condPaletteRect.left + pad, condPaletteRect.top + pad, condPaletteRect.right - pad,
                       condPaletteRect.top + pad + rowH * 0.8f };
    dc.drawAutoFitted(header, LocalizeString::get("client.ui.clickGui.keybinds.cond.palette.name"),
                            d2d::Color(1.f, 1.f, 1.f, 0.65f), FontSelection::PrimaryRegular, rowH * 0.55f,
                            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    auto* selected = condCanvas.selectedNode != 0 ? graph.find(condCanvas.selectedNode) : nullptr;
    float inspectorH = selected ? rowH * 7.4f : 0.f;
    float listBottom = condPaletteRect.bottom - pad - inspectorH;

    d2d::Rect listArea { condPaletteRect.left + pad, header.bottom + rowGap, condPaletteRect.right - pad, listBottom };

    float fullH = static_cast<float>(std::size(paletteKinds)) * (rowH + rowGap);
    condCanvas.paletteScrollMax = std::max(0.f, fullH - listArea.getHeight());
    condCanvas.paletteScroll = std::clamp(condCanvas.paletteScroll, 0.f, condCanvas.paletteScrollMax);

    dc.ctx->PushAxisAlignedClip(listArea.get(), D2D1_ANTIALIAS_MODE_ALIASED);

    bool inList = listArea.contains(cursorPos) && shouldSelect(listArea, cursorPos);
    float y = listArea.top - condCanvas.paletteScroll;
    CondKind addKind = CondKind::Invalid;

    for (auto kind : paletteKinds) {
        d2d::Rect rowRc { listArea.left, y, listArea.right, y + rowH };
        if (rowRc.bottom > listArea.top && rowRc.top < listArea.bottom) {
            bool hovered = inList && rowRc.contains(cursorPos);
            auto col = kindColor(kind);
            dc.fillRoundedRectangle(rowRc, col.asAlpha(hovered ? 0.92f : 0.62f), rowH * 0.26f);
            dc.drawAutoFitted({ rowRc.left + pad * 0.5f, rowRc.top, rowRc.right - pad * 0.5f, rowRc.bottom },
                                    kindLabel(kind), d2d::Colors::WHITE, FontSelection::PrimaryRegular, rowH * 0.56f,
                                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            if (hovered) {
                setTooltip(LocalizeString::get("client.ui.clickGui.keybinds.cond.add.desc"));
                if (justClicked[0]) addKind = kind;
            }
        }
        y += rowH + rowGap;
    }

    dc.ctx->PopAxisAlignedClip();

    // pressing a palette row arms a drag; if the user never moves it falls through to click-to-add below
    if (addKind != CondKind::Invalid && !condCanvas.dragging && !condCanvas.dragArmed) {
        condCanvas.dragArmed = true;
        condCanvas.dragNode = 0;
        condCanvas.dragNewKind = static_cast<int>(addKind);
        condCanvas.dragStart = cursorPos;
        return;
    }

    if (!selected) return;

    d2d::Rect inspector { condPaletteRect.left + pad, listBottom + rowGap, condPaletteRect.right - pad,
                          condPaletteRect.bottom - pad };
    dc.fillRoundedRectangle(inspector, d2d::Color::RGB(0x22, 0x22, 0x22).asAlpha(0.92f), rowH * 0.24f);

    float iy = inspector.top + rowGap * 0.8f;
    float ctrlH = rowH * 1.08f;
    auto lineRect = [&](float height) {
        d2d::Rect rc { inspector.left + pad * 0.5f, iy, inspector.right - pad * 0.5f, iy + height };
        iy += height + rowGap * 0.5f;
        return rc;
    };

    dc.drawAutoFitted(lineRect(ctrlH * 0.85f), nodeSummary(*selected), d2d::Colors::WHITE,
                            FontSelection::PrimarySemilight, ctrlH * 0.5f, DWRITE_TEXT_ALIGNMENT_LEADING,
                            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (ConditionGraph::usesThreshold(selected->kind)) {
        d2d::Rect row = lineRect(ctrlH);
        float seg = row.getWidth() / 4.f;
        d2d::Rect cmpRc { row.left, row.top, row.left + seg * 0.75f, row.bottom };
        if (drawButton(dc, cmpRc, compareSymbol(selected->compare),
                       LocalizeString::get("client.ui.clickGui.keybinds.cond.compare.desc").value(), true)) {
            selected->compare = static_cast<CondCompare>((static_cast<int>(selected->compare) + 1) %
                                                         static_cast<int>(CondCompare::Count));
            mgr.markDirty();
            playClickSound();
        }

        d2d::Rect minusRc { cmpRc.right + seg * 0.08f, row.top, cmpRc.right + seg * 0.08f + seg * 0.55f, row.bottom };
        d2d::Rect valRc { minusRc.right, row.top, minusRc.right + seg * 1.2f, row.bottom };
        d2d::Rect plusRc { valRc.right, row.top, valRc.right + seg * 0.55f, row.bottom };

        float step = (selected->kind == CondKind::Health && selected->percent) ? 5.f : 1.f;
        if (drawButton(dc, minusRc, L"-", L"", true)) {
            selected->threshold = std::max(-10000.f, selected->threshold - step);
            mgr.markDirty();
            playClickSound();
        }

        {
            std::wstring val = formatNumber(selected->threshold);
            if (condThresholdNode != selected->id) {
                condThresholdBox.setSelected(false);
                condThresholdBox.setText(val);
                condThresholdBox.setCaretLocation(static_cast<int>(val.size()));
                condThresholdNode = selected->id;
            } else if (!condThresholdBox.isSelected()) {
                condThresholdBox.setText(val);
                condThresholdBox.setCaretLocation(static_cast<int>(val.size()));
            }
            condThresholdBox.setRect(valRc);
            condThresholdBox.render(dc, valRc.getHeight() * 0.2f, d2d::Color::RGB(0x38, 0x38, 0x38).asAlpha(0.95f),
                                    d2d::Colors::WHITE);
            dc.drawRoundedRectangle(valRc,
                                    d2d::Color(1.f, 1.f, 1.f, condThresholdBox.isSelected() ? 0.55f : 0.18f),
                                    valRc.getHeight() * 0.2f, 1.f, DrawUtil::OutlinePosition::Inside);

            if (justClicked[0]) {
                if (valRc.contains(cursorPos)) {
                    condThresholdBox.setSelected(true);
                    playClickSound();
                } else {
                    condThresholdBox.setSelected(false);
                }
            }

            if (condThresholdBox.isSelected()) {
                auto text = condThresholdBox.getText();
                if (!text.empty()) {
                    try {
                        float parsed = std::clamp(std::stof(text), -10000.f, 10000.f);
                        if (parsed != selected->threshold) {
                            selected->threshold = parsed;
                            mgr.markDirty();
                        }
                    } catch (...) {}
                }
            }
        }

        if (drawButton(dc, plusRc, L"+", L"", true)) {
            selected->threshold = std::min(10000.f, selected->threshold + step);
            mgr.markDirty();
            playClickSound();
        }

        if (selected->kind == CondKind::Health) {
            d2d::Rect pctRc { plusRc.right + seg * 0.08f, row.top, row.right, row.bottom };
            if (drawButton(dc, pctRc, selected->percent ? L"%" : L"HP",
                           LocalizeString::get("client.ui.clickGui.keybinds.cond.percent.desc").value(), true)) {
                selected->percent = !selected->percent;
                selected->threshold = selected->percent ? 50.f : 10.f;
                mgr.markDirty();
                playClickSound();
            }
        }
    }

    if (selected->kind == CondKind::EnemiesInRange) {
        d2d::Rect row = lineRect(ctrlH);
        float seg = row.getWidth() / 6.f;
        d2d::Rect cMinus { row.left, row.top, row.left + seg * 0.8f, row.bottom };
        d2d::Rect cVal { cMinus.right, row.top, cMinus.right + seg * 1.2f, row.bottom };
        d2d::Rect cPlus { cVal.right, row.top, cVal.right + seg * 0.8f, row.bottom };
        if (drawButton(dc, cMinus, L"-", L"", true)) {
            selected->count = std::max(1, selected->count - 1);
            mgr.markDirty();
            playClickSound();
        }

        {
            std::wstring val = std::to_wstring(selected->count) + L"x";
            if (condCountNode != selected->id) {
                condCountBox.setSelected(false);
                condCountBox.setText(val);
                condCountBox.setCaretLocation(static_cast<int>(val.size()));
                condCountNode = selected->id;
            } else if (!condCountBox.isSelected()) {
                condCountBox.setText(val);
                condCountBox.setCaretLocation(static_cast<int>(val.size()));
            }
            condCountBox.setRect(cVal);
            condCountBox.render(dc, cVal.getHeight() * 0.2f, d2d::Color::RGB(0x38, 0x38, 0x38).asAlpha(0.95f),
                                d2d::Colors::WHITE);
            dc.drawRoundedRectangle(cVal,
                                    d2d::Color(1.f, 1.f, 1.f, condCountBox.isSelected() ? 0.55f : 0.18f),
                                    cVal.getHeight() * 0.2f, 1.f, DrawUtil::OutlinePosition::Inside);

            if (justClicked[0]) {
                if (cVal.contains(cursorPos)) {
                    condCountBox.setSelected(true);
                    playClickSound();
                } else {
                    condCountBox.setSelected(false);
                }
            }

            if (condCountBox.isSelected()) {
                auto text = condCountBox.getText();
                auto xPos = text.find(L'x');
                if (xPos != std::string::npos) text = text.substr(0, xPos);
                if (!text.empty()) {
                    try {
                        int parsed = std::clamp(std::stoi(text), 1, 256);
                        if (parsed != selected->count) {
                            selected->count = parsed;
                            mgr.markDirty();
                        }
                    } catch (...) {}
                }
            }
        }

        if (drawButton(dc, cPlus, L"+", L"", true)) {
            selected->count = std::min(256, selected->count + 1);
            mgr.markDirty();
            playClickSound();
        }

        d2d::Rect rMinus { cPlus.right + seg * 0.1f, row.top, cPlus.right + seg * 0.9f, row.bottom };
        d2d::Rect rVal { rMinus.right, row.top, rMinus.right + seg * 1.3f, row.bottom };
        d2d::Rect rPlus { rVal.right, row.top, row.right, row.bottom };
        if (drawButton(dc, rMinus, L"-", L"", true)) {
            selected->range = std::max(0.f, selected->range - 1.f);
            mgr.markDirty();
            playClickSound();
        }

        {
            std::wstring val = formatNumber(selected->range) + L"m";
            if (condRangeNode != selected->id) {
                condRangeBox.setSelected(false);
                condRangeBox.setText(val);
                condRangeBox.setCaretLocation(static_cast<int>(val.size()));
                condRangeNode = selected->id;
            } else if (!condRangeBox.isSelected()) {
                condRangeBox.setText(val);
                condRangeBox.setCaretLocation(static_cast<int>(val.size()));
            }
            condRangeBox.setRect(rVal);
            condRangeBox.render(dc, rVal.getHeight() * 0.2f, d2d::Color::RGB(0x38, 0x38, 0x38).asAlpha(0.95f),
                                d2d::Colors::WHITE);
            dc.drawRoundedRectangle(rVal,
                                    d2d::Color(1.f, 1.f, 1.f, condRangeBox.isSelected() ? 0.55f : 0.18f),
                                    rVal.getHeight() * 0.2f, 1.f, DrawUtil::OutlinePosition::Inside);

            if (justClicked[0]) {
                if (rVal.contains(cursorPos)) {
                    condRangeBox.setSelected(true);
                    playClickSound();
                } else {
                    condRangeBox.setSelected(false);
                }
            }

            if (condRangeBox.isSelected()) {
                auto text = condRangeBox.getText();
                auto mPos = text.find(L'm');
                if (mPos != std::string::npos) text = text.substr(0, mPos);
                if (!text.empty()) {
                    try {
                        float parsed = std::clamp(std::stof(text), 0.f, 128.f);
                        if (parsed != selected->range) {
                            selected->range = parsed;
                            mgr.markDirty();
                        }
                    } catch (...) {}
                }
            }
        }

        if (drawButton(dc, rPlus, L"+", L"", true)) {
            selected->range = std::min(128.f, selected->range + 1.f);
            mgr.markDirty();
            playClickSound();
        }
    }

    if (selected->kind == CondKind::TookDamage || selected->kind == CondKind::DealtDamage) {
        d2d::Rect row = lineRect(ctrlH);
        float seg = row.getWidth() / 3.f;
        d2d::Rect wMinus { row.left, row.top, row.left + seg * 0.75f, row.bottom };
        d2d::Rect wVal { wMinus.right, row.top, wMinus.right + seg * 1.5f, row.bottom };
        d2d::Rect wPlus { wVal.right, row.top, row.right, row.bottom };
        if (drawButton(dc, wMinus, L"-", L"", true)) {
            selected->windowMs = std::max(50.f, selected->windowMs - 250.f);
            mgr.markDirty();
            playClickSound();
        }

        {
            std::wstring val = formatNumber(selected->windowMs / 1000.f) + L"s";
            if (condDamageWindowNode != selected->id) {
                condDamageWindowBox.setSelected(false);
                condDamageWindowBox.setText(val);
                condDamageWindowBox.setCaretLocation(static_cast<int>(val.size()));
                condDamageWindowNode = selected->id;
            } else if (!condDamageWindowBox.isSelected()) {
                condDamageWindowBox.setText(val);
                condDamageWindowBox.setCaretLocation(static_cast<int>(val.size()));
            }
            condDamageWindowBox.setRect(wVal);
            condDamageWindowBox.render(dc, wVal.getHeight() * 0.2f, d2d::Color::RGB(0x38, 0x38, 0x38).asAlpha(0.95f),
                                       d2d::Colors::WHITE);
            dc.drawRoundedRectangle(wVal,
                                    d2d::Color(1.f, 1.f, 1.f, condDamageWindowBox.isSelected() ? 0.55f : 0.18f),
                                    wVal.getHeight() * 0.2f, 1.f, DrawUtil::OutlinePosition::Inside);

            if (justClicked[0]) {
                if (wVal.contains(cursorPos)) {
                    condDamageWindowBox.setSelected(true);
                    playClickSound();
                } else {
                    condDamageWindowBox.setSelected(false);
                }
            }

            if (condDamageWindowBox.isSelected()) {
                auto text = condDamageWindowBox.getText();
                auto sPos = text.find(L's');
                if (sPos != std::string::npos) text = text.substr(0, sPos);
                if (!text.empty()) {
                    try {
                        float parsed = std::clamp(std::stof(text) * 1000.f, 50.f, 10000.f);
                        parsed = std::round(parsed / 250.f) * 250.f;
                        if (parsed != selected->windowMs) {
                            selected->windowMs = parsed;
                            mgr.markDirty();
                        }
                    } catch (...) {}
                }
            }
        }

        if (drawButton(dc, wPlus, L"+", L"", true)) {
            selected->windowMs = std::min(10000.f, selected->windowMs + 250.f);
            mgr.markDirty();
            playClickSound();
        }
    }

    if (selected->kind == CondKind::Wait) {
        d2d::Rect row = lineRect(ctrlH);
        std::wstring value = std::to_wstring(static_cast<int>(std::round(selected->waitMs)));
        if (condWaitMsNode != selected->id) {
            condWaitMsBox.setSelected(false);
            condWaitMsBox.setText(value);
            condWaitMsBox.setCaretLocation(static_cast<int>(value.size()));
            condWaitMsNode = selected->id;
        } else if (!condWaitMsBox.isSelected()) {
            condWaitMsBox.setText(value);
            condWaitMsBox.setCaretLocation(static_cast<int>(value.size()));
        }

        float labelW = row.getWidth() * 0.36f;
        d2d::Rect labelRc { row.left, row.top, row.left + labelW, row.bottom };
        d2d::Rect inputRc { labelRc.right + row.getWidth() * 0.04f, row.top, row.right, row.bottom };
        dc.drawAutoFitted(labelRc, LocalizeString::get("client.ui.clickGui.keybinds.cond.waitMs.name"),
                          d2d::Color(1.f, 1.f, 1.f, 0.72f), FontSelection::PrimaryRegular, ctrlH * 0.48f,
                          DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        condWaitMsBox.setRect(inputRc);
        condWaitMsBox.render(dc, inputRc.getHeight() * 0.2f, d2d::Color::RGB(0x38, 0x38, 0x38).asAlpha(0.95f),
                             d2d::Colors::WHITE);
        dc.drawRoundedRectangle(inputRc,
                                d2d::Color(1.f, 1.f, 1.f, condWaitMsBox.isSelected() ? 0.55f : 0.18f),
                                inputRc.getHeight() * 0.2f, 1.f, DrawUtil::OutlinePosition::Inside);

        if (justClicked[0]) {
            if (inputRc.contains(cursorPos)) {
                condWaitMsBox.setSelected(true);
                playClickSound();
            } else {
                condWaitMsBox.setSelected(false);
            }
        }

        if (condWaitMsBox.isSelected()) {
            auto text = condWaitMsBox.getText();
            if (!text.empty()) {
                try {
                    float parsed = std::clamp(std::stof(text), 0.f, 600000.f);
                    parsed = std::round(parsed);
                    if (parsed != selected->waitMs) {
                        selected->waitMs = parsed;
                        graph.resetRuntime();
                        mgr.markDirty();
                    }
                } catch (...) {}
            }
        }
    } else if (condWaitMsBox.isSelected()) {
        condWaitMsBox.setSelected(false);
        condWaitMsNode = 0;
    }

    if (condThresholdBox.isSelected() && !ConditionGraph::usesThreshold(selected->kind)) {
        condThresholdBox.setSelected(false);
        condThresholdNode = 0;
    }
    if (condCountBox.isSelected() && selected->kind != CondKind::EnemiesInRange) {
        condCountBox.setSelected(false);
        condCountNode = 0;
    }
    if (condRangeBox.isSelected() && selected->kind != CondKind::EnemiesInRange) {
        condRangeBox.setSelected(false);
        condRangeNode = 0;
    }
    if (condDamageWindowBox.isSelected() && selected->kind != CondKind::TookDamage && selected->kind != CondKind::DealtDamage) {
        condDamageWindowBox.setSelected(false);
        condDamageWindowNode = 0;
    }

    if (selected->kind == CondKind::HoldingItem) {
        d2d::Rect row = lineRect(ctrlH);
        std::wstring label = selected->itemId.empty()
            ? LocalizeString::get("client.ui.clickGui.keybinds.cond.pickItem.name").value()
            : ItemCatalog::get().displayNameFor(selected->itemId);
        if (drawButton(dc, row, label, LocalizeString::get("client.ui.clickGui.keybinds.cond.pickItem.desc").value(),
                       true)) {
            condCanvas.itemPickerNode = selected->id;
            condCanvas.itemPickerJustOpened = true;
            condCanvas.itemScroll = 0.f;
            condItemSearchBox.reset();
            condItemSearchBox.setSelected(true);
            ItemCatalog::get().entries();
            playClickSound();
        }
    }

    d2d::Rect actionRow = lineRect(ctrlH);
    float actSeg = actionRow.getWidth() / 3.f;
    d2d::Rect invRc { actionRow.left, actionRow.top, actionRow.left + actSeg * 0.95f, actionRow.bottom };
    d2d::Rect rootRc { invRc.right + actSeg * 0.05f, actionRow.top, invRc.right + actSeg * 1.f, actionRow.bottom };
    d2d::Rect delRc { rootRc.right + actSeg * 0.05f, actionRow.top, actionRow.right, actionRow.bottom };

    if (drawButton(dc, invRc,
                   selected->invert ? LocalizeString::get("client.ui.clickGui.keybinds.cond.inverted.name").value()
                                    : LocalizeString::get("client.ui.clickGui.keybinds.cond.invert.name").value(),
                   LocalizeString::get("client.ui.clickGui.keybinds.cond.invert.desc").value(), true)) {
        selected->invert = !selected->invert;
        mgr.markDirty();
        playClickSound();
    }

    bool isRoot = selected->id == graph.getRoot();
    if (drawButton(dc, rootRc, LocalizeString::get("client.ui.clickGui.keybinds.cond.root.name").value(),
                   LocalizeString::get("client.ui.clickGui.keybinds.cond.root.desc").value(), !isRoot)) {
        graph.setRoot(selected->id);
        mgr.markDirty();
        playClickSound();
    }

    if (drawButton(dc, delRc, LocalizeString::get("client.ui.clickGui.keybinds.cond.delete.name").value(),
                   LocalizeString::get("client.ui.clickGui.keybinds.cond.delete.desc").value(), true)) {
        int toRemove = selected->id;
        if (graph.removeNode(toRemove)) {
            if (condCanvas.selectedNode == toRemove) condCanvas.selectedNode = 0;
            graph.sanitize();
            mgr.markDirty();
        }
        playClickSound();
    }
}

void ClickGUI::drawCondItemPicker(D2DUtil& dc) {
    auto& mgr = KeybindManager::get();
    auto* bind = mgr.findBind(condCanvas.bind);
    if (!bind) {
        condCanvas.itemPickerNode = 0;
        return;
    }
    auto* node = bind->graph.find(condCanvas.itemPickerNode);
    if (!node) {
        condCanvas.itemPickerNode = 0;
        return;
    }

    auto& cursorPos = SDK::ClientInstance::get()->cursorPos;
    auto accentColor = d2d::Color(Necromancer::get().getAccentColor().getMainColor());

    float w = 0.38f * rect.getWidth();
    float h = 0.62f * rect.getHeight();
    condItemPickerRect = { rect.centerX() - w * 0.5f, rect.centerY() - h * 0.5f, rect.centerX() + w * 0.5f,
                           rect.centerY() + h * 0.5f };

    float pad = 0.03f * w;
    float titleH = 0.062f * w;
    float rowH = rect.getHeight() * 0.05f;

    dc.fillRoundedRectangle(condItemPickerRect, d2d::Color::RGB(0x7, 0x7, 0x7).asAlpha(0.96f), 16.f * adaptedScale);
    dc.drawRoundedRectangle(condItemPickerRect, accentColor.asAlpha(0.8f), 16.f * adaptedScale, 2.f * adaptedScale,
                            DrawUtil::OutlinePosition::Inside);

    d2d::Rect titleRect { condItemPickerRect.left + pad, condItemPickerRect.top + pad,
                          condItemPickerRect.right - pad - titleH, condItemPickerRect.top + pad + titleH };
    dc.drawAutoFitted(titleRect, LocalizeString::get("client.ui.clickGui.keybinds.cond.itemPicker.name"),
                            d2d::Colors::WHITE, FontSelection::PrimaryLight, titleH * 0.6f,
                            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    d2d::Rect xRect { condItemPickerRect.right - pad - titleH * 0.75f, condItemPickerRect.top + pad + titleH * 0.12f,
                      condItemPickerRect.right - pad, condItemPickerRect.top + pad + titleH * 0.87f };
    if (auto* bmp = Necromancer::getAssets().xIcon.getBitmap()) dc.ctx->DrawBitmap(bmp, xRect.get());
    if (justClicked[0] && !condCanvas.itemPickerJustOpened && xRect.contains(cursorPos)) {
        condCanvas.itemPickerNode = 0;
        condItemSearchBox.setSelected(false);
        playClickSound();
        return;
    }

    d2d::Rect searchRc { condItemPickerRect.left + pad, titleRect.bottom + pad * 0.6f, condItemPickerRect.right - pad,
                         titleRect.bottom + pad * 0.6f + rowH * 0.85f };
    condItemSearchBox.setRect(searchRc);
    condItemSearchBox.render(dc, searchRc.getHeight() * 0.25f, d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.11f),
                             d2d::Colors::WHITE);
    dc.drawRoundedRectangle(searchRc, d2d::Color(1.f, 1.f, 1.f, condItemSearchBox.isSelected() ? 0.55f : 0.18f),
                            searchRc.getHeight() * 0.25f, 1.f, DrawUtil::OutlinePosition::Inside);
    if (justClicked[0] && searchRc.contains(cursorPos)) condItemSearchBox.setSelected(true);

    d2d::Rect listArea { condItemPickerRect.left + pad, searchRc.bottom + pad * 0.6f, condItemPickerRect.right - pad,
                         condItemPickerRect.bottom - pad };

    std::wstring search = condItemSearchBox.getText();
    std::ranges::transform(search, search.begin(), towlower);

    auto const& catalog = ItemCatalog::get().entries();

    std::vector<ItemCatalog::Entry const*> filtered;
    filtered.reserve(catalog.size());
    for (auto const& entry : catalog) {
        if (!search.empty() && entry.searchKey.find(search) == std::wstring::npos) continue;
        filtered.push_back(&entry);
    }

    float rowGap = rowH * 0.12f;
    float fullH = static_cast<float>(filtered.size()) * (rowH + rowGap);
    condCanvas.itemScrollMax = std::max(0.f, fullH - listArea.getHeight());
    condCanvas.itemScroll = std::clamp(condCanvas.itemScroll, 0.f, condCanvas.itemScrollMax);

    dc.ctx->PushAxisAlignedClip(listArea.get(), D2D1_ANTIALIAS_MODE_ALIASED);

    if (filtered.empty()) {
        dc.drawAutoFitted(listArea, LocalizeString::get("client.ui.clickGui.keybinds.cond.noItems.name"),
                                d2d::Color(1.f, 1.f, 1.f, 0.45f), FontSelection::PrimaryRegular, rowH * 0.42f,
                                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    int firstVisible = static_cast<int>(condCanvas.itemScroll / (rowH + rowGap));
    int maxVisible = static_cast<int>(listArea.getHeight() / (rowH + rowGap)) + 2;
    bool picked = false;

    for (int i = std::max(0, firstVisible); i < static_cast<int>(filtered.size()) && i < firstVisible + maxVisible;
         i++) {
        auto const* entry = filtered[i];
        float ry = listArea.top - condCanvas.itemScroll + static_cast<float>(i) * (rowH + rowGap);
        d2d::Rect rowRc { listArea.left, ry, listArea.right, ry + rowH };
        if (rowRc.bottom < listArea.top || rowRc.top > listArea.bottom) continue;

        bool selectedRow = entry->id == node->itemId;
        bool hovered = rowRc.contains(cursorPos) && listArea.contains(cursorPos);
        auto bg = selectedRow ? accentColor.asAlpha(0.5f)
            : hovered         ? d2d::Color::RGB(0xD9, 0xD9, 0xD9).asAlpha(0.18f)
                              : d2d::Color::RGB(0x8D, 0x8D, 0x8D).asAlpha(0.1f);
        dc.fillRoundedRectangle(rowRc, bg, rowH * 0.22f);

        float iconPad = rowH * 0.14f;
        float iconSize = rowH - iconPad * 2.f;
        d2d::Rect iconRc { rowRc.left + iconPad, rowRc.top + iconPad, rowRc.left + iconPad + iconSize,
                           rowRc.bottom - iconPad };
        if (entry->block) {
            condIconDraws.emplace_back(iconRc, entry->block);
        } else {
            dc.fillRoundedRectangle(iconRc, d2d::Color::RGB(0x3A, 0x3A, 0x3A).asAlpha(0.9f), iconSize * 0.2f);
            std::wstring initial = entry->displayName.empty() ? L"?" : entry->displayName.substr(0, 1);
            dc.drawAutoFitted(iconRc, initial, d2d::Color(1.f, 1.f, 1.f, 0.75f), FontSelection::PrimaryRegular,
                                    iconSize * 0.5f, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        dc.drawAutoFitted({ iconRc.right + iconPad, rowRc.top, rowRc.right - iconPad, rowRc.bottom },
                                entry->displayName, d2d::Colors::WHITE, FontSelection::PrimaryRegular, rowH * 0.44f,
                                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        if (hovered && justClicked[0]) {
            node->itemId = entry->id;
            mgr.markDirty();
            picked = true;
            playClickSound();
            break;
        }
    }

    dc.ctx->PopAxisAlignedClip();

    if (picked) {
        condCanvas.itemPickerNode = 0;
        condCanvas.itemPickerJustOpened = false;
        condItemSearchBox.setSelected(false);
        return;
    }

    if (condCanvas.itemPickerJustOpened) {
        condCanvas.itemPickerJustOpened = false;
    } else if (justClicked[0] && !condItemPickerRect.contains(cursorPos)) {
        condCanvas.itemPickerNode = 0;
        condItemSearchBox.setSelected(false);
    }
}
