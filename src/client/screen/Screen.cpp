#include "pch.h"
#include "Screen.h"
#include "client/Necromancer.h"
#include "client/event/Eventing.h"
#include "ScreenManager.h"
#include "client/event/events/ClickEvent.h"
#include "client/event/events/RenderOverlayEvent.h"
#include "client/event/events/UpdateEvent.h"

Screen::Screen() {
    /*
    arrow = LoadCursorW(Necromancer::get().dllInst, IDC_ARROW);
    hand = LoadCursorW(Necromancer::get().dllInst, IDC_HAND);
    ibeam = LoadCursorW(Necromancer::get().dllInst, IDC_IBEAM);
    */
    // ^ this doesnt work with resources...

    Eventing::get().listen<UpdateEvent>(this, (EventListenerFunc)&Screen::onUpdate, 0);
    Eventing::get().listen<RenderOverlayEvent>(this, (EventListenerFunc)&Screen::onRenderOverlay, 0, true);
    Eventing::get().listen<ClickEvent>(this, (EventListenerFunc)&Screen::onClick, 3, true);
}

void Screen::onUpdate(Event& ev) {
    // clang-format off
    /*
    switch (cursor) {
    case Cursor::Arrow:
        SetCursor(arrow);
        break;
    case Cursor::Hand:
        SetCursor(hand);
        break;
    case Cursor::IBeam:
        SetCursor(ibeam);
        break;
    }
    */
    // clang-format on
}

void Screen::close() {
    resetInputState();
    Necromancer::getScreenManager().exitCurrentScreen();
}

void Screen::resetInputState() {
    this->activeMouseButtons = { false, false, false };
    this->mouseButtons = { false, false, false };
    this->justClicked = { false, false, false };
}

void Screen::playClickSound() {
    util::PlaySoundUI("random.click");
}

void Screen::setTooltip(std::optional<std::wstring> newTooltip) {
    this->tooltip = newTooltip;
}

void Screen::onClick(Event& evGeneric) {
    auto& ev = reinterpret_cast<ClickEvent&>(evGeneric);
    if (ev.getMouseButton() > 0) {
        if (ev.getMouseButton() < 4) {
            if (isActive()) {
                if (ev.isDown()) this->activeMouseButtons[ev.getMouseButton() - 1] = ev.isDown();
                this->mouseButtons[ev.getMouseButton() - 1] = ev.isDown();
            }
            if (isActive()) ev.setCancelled(true);
        }
    }
}

void Screen::onRenderOverlay(Event& ev) {
    if (this->isActive()) {
        for (size_t i = 0; i < justClicked.size(); i++) {
            justClicked[i] = this->activeMouseButtons[i];
            this->activeMouseButtons[i] = false;
        }

        if (this->tooltip != oldTooltip) {
            this->lastTooltipChange = std::chrono::system_clock::now();
            oldTooltip = tooltip;
        }
    }

    if (isActive() && this->tooltip.has_value()) {
        auto now = std::chrono::system_clock::now();
        if (now - lastTooltipChange >= 500ms) {
            D2DUtil dc;
            Vec2& mousePos = SDK::ClientInstance::get()->cursorPos;

            auto screenPx = dc.ctx ? dc.ctx->GetPixelSize() : D2D1_SIZE_U { 1920, 1080 };
            float screenW = static_cast<float>(screenPx.width);
            float screenH = static_cast<float>(screenPx.height);

            // font scales with the viewport instead of being pinned to 15px
            float fontSize = std::clamp(screenH * 0.0165f, 9.f, 26.f);
            float pad = fontSize * 0.55f;

            // never let the bubble exceed the screen; shrink the text until it fits the allowance
            float maxBubbleW = screenW * 0.42f;
            std::wstring const& tip = this->tooltip.value();
            d2d::Rect allowance { 0.f, 0.f, maxBubbleW - pad * 2.f, fontSize * 1.6f };
            fontSize = dc.fitTextSize(tip, Renderer::FontSelection::PrimaryRegular, allowance, fontSize, 8.f);
            pad = fontSize * 0.55f;

            d2d::Rect textRect = dc.getTextRect(tip, Renderer::FontSelection::PrimaryRegular, fontSize, pad);
            textRect.setPos(mousePos);

            float w = textRect.getWidth();
            float h = textRect.getHeight();

            // place above-right of the cursor, then clamp fully inside the viewport
            float left = mousePos.x + fontSize * 0.35f;
            float top = mousePos.y - h * 0.9f;
            left = std::clamp(left, 2.f, std::max(2.f, screenW - w - 2.f));
            top = std::clamp(top, 2.f, std::max(2.f, screenH - h - 2.f));

            textRect = { left, top, left + w, top + h };

            float rad = h * 0.25f;
            dc.fillRoundedRectangle(textRect, d2d::Color(0.f, 0.f, 0.f, 0.6f), rad);
            dc.drawRoundedRectangle(textRect, d2d::Color(0.9f, 0.9f, 0.9f, 1.f), rad, 1.f);
            dc.drawAutoFitted(textRect, tip, d2d::Color(1.f, 1.f, 1.f, 0.8f), Renderer::FontSelection::PrimaryRegular,
                              fontSize, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 8.f);
        }
    }
    this->tooltip = std::nullopt;
}
