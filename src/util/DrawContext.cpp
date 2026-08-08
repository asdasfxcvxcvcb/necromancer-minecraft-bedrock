#include "pch.h"
#include "DrawContext.h"
#include "client/Necromancer.h"
#include "Util.h"
#include "mc/common/client/renderer/Tessellator.h"
#include "mc/common/client/renderer/MeshUtils.h"
#include "mc/common/client/renderer/MaterialPtr.h"
#include "mc/common/client/renderer/ActorRenderDispatcher.h"
#include "mc/common/entity/component/ActorHeadRotationComponent.h"
#include "mc/common/entity/component/MobBodyRotationComponent.h"
#include "mc/common/entity/component/RenderRotationComponent.h"
#include "mc/common/world/actor/Actor.h"
#include <mc/common/client/renderer/game/BaseActorRenderContext.h>
#include <mc/common/client/renderer/ItemRenderer.h>
#include <glm/gtc/matrix_transform.hpp>
#include <ranges>
#include <utility>

static size_t countof(auto str, auto ch) {
    size_t c = 0;
    for (auto cha : str) {
        if (cha == ch) ++c;
    }
    return c;
}

#ifdef NECROMANCER_DEBUG
static d2d::Color getDebugTextRectColor(bool overflow) {
    return overflow ? d2d::Color::RGB(0xFF, 0x35, 0x35).asAlpha(0.95f)
                    : d2d::Color::RGB(0x22, 0xD7, 0xFF).asAlpha(0.75f);
}

static bool isDebugTextOverflow(Vec2 const& textSize, d2d::Rect const& rect) {
    constexpr float epsilon = 0.5f;
    return textSize.x > rect.getWidth() + epsilon || textSize.y > rect.getHeight() + epsilon;
}
#endif

static DWRITE_READING_DIRECTION getSelectedTextReadingDirection() {
    try {
        return Necromancer::get().getL10nData().isSelectedLanguageRightToLeft() ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT
                                                                           : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    } catch (...) {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }
}

static void applySelectedTextDirection(IDWriteTextLayout* layout) {
    if (!layout) return;

    layout->SetReadingDirection(getSelectedTextReadingDirection());
    layout->SetFlowDirection(DWRITE_FLOW_DIRECTION_TOP_TO_BOTTOM);
}

static std::wstring singleLineText(std::wstring text) {
    for (auto& ch : text) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') {
            ch = L' ';
        }
    }

    return text;
}

D2D1_RECT_F D2DUtil::getRect(RectF const& rc) {
    return D2D1::RectF(rc.left, rc.top, rc.right, rc.bottom);
}

void D2DUtil::drawBitmapMirroredX(ID2D1Bitmap* bitmap, RectF const& rect, bool mirror, float opacity) {
    if (!mirror) {
        ctx->DrawBitmap(bitmap, rect.get(), opacity);
        return;
    }

    D2D1::Matrix3x2F oldTransform;
    ctx->GetTransform(&oldTransform);
    ctx->SetTransform(D2D1::Matrix3x2F::Scale(-1.f, 1.f, { rect.centerX(), rect.centerY() }) * oldTransform);
    ctx->DrawBitmap(bitmap, rect.get(), opacity);
    ctx->SetTransform(oldTransform);
}

void D2DUtil::drawBitmapRotated(ID2D1Bitmap* bitmap, RectF const& rect, float degrees, float opacity) {
    D2D1::Matrix3x2F oldTransform;
    ctx->GetTransform(&oldTransform);
    ctx->SetTransform(D2D1::Matrix3x2F::Rotation(degrees, { rect.centerX(), rect.centerY() }) * oldTransform);
    ctx->DrawBitmap(bitmap, rect.get(), opacity);
    ctx->SetTransform(oldTransform);
}

void D2DUtil::fillRectangle(RectF const& rect, d2d::Color const& color) {
    this->brush->SetColor(color.get());
    ctx->FillRectangle(getRect(rect), brush);
}

void D2DUtil::drawRectangle(RectF const& rect, d2d::Color const& color, float lineThickness) {
    this->brush->SetColor(color.get());
    ctx->DrawRectangle(getRect(rect), brush, lineThickness);
}

void D2DUtil::fillRectangle(RectF const& rect, ID2D1Brush* cbrush) {
    ctx->FillRectangle(getRect(rect), cbrush);
}

void D2DUtil::drawRectangle(RectF const& rect, ID2D1Brush* cbrush, float lineThickness) {
    ctx->DrawRectangle(getRect(rect), cbrush, lineThickness);
}

void D2DUtil::fillTriangle(Vec2 const& a, Vec2 const& b, Vec2 const& c, d2d::Color const& col) {
    auto* d2dFactory = Necromancer::getRenderer().getFactory();
    if (!d2dFactory) return;

    ComPtr<ID2D1PathGeometry> geometry;
    if (FAILED(d2dFactory->CreatePathGeometry(geometry.GetAddressOf()))) return;

    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(sink.GetAddressOf()))) return;

    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    sink->BeginFigure(D2D1::Point2F(a.x, a.y), D2D1_FIGURE_BEGIN_FILLED);
    const D2D1_POINT_2F points[2] = { D2D1::Point2F(b.x, b.y), D2D1::Point2F(c.x, c.y) };
    sink->AddLines(points, 2);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

    if (FAILED(sink->Close())) return;

    this->brush->SetColor(col.get());
    ctx->FillGeometry(geometry.Get(), brush);
}

void D2DUtil::fillRoundedRectangle(RectF const& rect, d2d::Color const& color, float radius) {
    this->brush->SetColor(color.get());
    if (radius < 0.005f) {
        ctx->FillRectangle(rect.get(), brush);
        return;
    }

    auto rounded = D2D1::RoundedRect(getRect(rect), radius, radius);
    ctx->FillRoundedRectangle(rounded, brush);
}

void D2DUtil::drawRoundedRectangle(RectF irect, d2d::Color const& color, float radius, float lineThickness,
                                   OutlinePosition outPos) {
    this->brush->SetColor(color.get());
    if (radius < 0.005f) {
        ctx->DrawRectangle(irect, brush, lineThickness);
        return;
    }

    RectF rect = irect;
    switch (outPos) {
    case OutlinePosition::Inside:
        rect.left += (lineThickness / 2.f);
        rect.right -= (lineThickness / 2.f);
        rect.top += (lineThickness / 2.f);
        rect.bottom -= (lineThickness / 2.f);
        break;
    case OutlinePosition::Outside:
        rect.left -= lineThickness / 2.f;
        rect.right += lineThickness / 2.f;
        rect.top -= lineThickness / 2.f;
        rect.bottom += lineThickness / 2.f;
        radius += lineThickness / 2.f;
        break;
    default:
        break;
    }
    auto rc = getRect(rect);
    auto rounded = D2D1::RoundedRect(rc, radius, radius);

    ctx->DrawRoundedRectangle(rounded, brush, lineThickness);
}

void D2DUtil::drawRoundedRectangle(RectF irect, ID2D1Brush* cbrush, float radius, float lineThickness,
                                   OutlinePosition outPos) {
    RectF rect = irect;
    switch (outPos) {
    case OutlinePosition::Inside:
        rect.left += (lineThickness / 2.f);
        rect.right -= (lineThickness / 2.f);
        rect.top += (lineThickness / 2.f);
        rect.bottom -= (lineThickness / 2.f);
        break;
    case OutlinePosition::Outside:
        rect.left -= lineThickness / 2.f;
        rect.right += lineThickness / 2.f;
        rect.top -= lineThickness / 2.f;
        rect.bottom += lineThickness / 2.f;
        radius += lineThickness / 2.f;
        break;
    default:
        break;
    }
    auto rc = getRect(rect);
    auto rounded = D2D1::RoundedRect(rc, radius, radius);

    ctx->DrawRoundedRectangle(rounded, cbrush, lineThickness);
}

void D2DUtil::fillRoundedRectangle(RectF const& rect, ID2D1Brush* cbrush, float radius) {
    auto rc = getRect(rect);
    auto rounded = D2D1::RoundedRect(rc, radius, radius);

    ctx->FillRoundedRectangle(rounded, cbrush);
}

void D2DUtil::drawGaussianBlur(float intensity) {
    ID2D1Effect* gaussianBlurEffect = Necromancer::getRenderer().getBlurEffect();

    // maybe we might not need to flush if we dont draw anything before clickgui?
    ctx->Flush();
    auto bitmap = Necromancer::getRenderer().getBlurBitmap();
    gaussianBlurEffect->SetInput(0, bitmap);
    gaussianBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, intensity);
    gaussianBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    gaussianBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);
    auto sz = ctx->GetPixelSize();
    auto rc = D2D1::RectF(0, 0, (float)sz.width, (float)sz.height);
    D2D1::Matrix3x2F oMat;
    // ctx->SetTransform(D2D1::Matrix3x2F::Scale(sz.width / rc.right, sz.height / rc.bottom));
    ctx->DrawImage(gaussianBlurEffect, D2D1::Point2F(0.f, 0.f), rc);
}

void D2DUtil::drawGaussianBlur(ID2D1Bitmap1* bmp, float intensity) {
    ID2D1Effect* gaussianBlurEffect = Necromancer::getRenderer().getBlurEffect();

    ctx->Flush();
    Necromancer::getRenderer().getCopiedBitmap(bmp);
    gaussianBlurEffect->SetInput(0, bmp);
    gaussianBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, intensity);
    gaussianBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    gaussianBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);
    auto sz = ctx->GetPixelSize();
    auto rc = D2D1::RectF(0, 0, (float)sz.width, (float)sz.height);
    D2D1::Matrix3x2F oMat;
    // ctx->SetTransform(D2D1::Matrix3x2F::Scale(sz.width / rc.right, sz.height / rc.bottom));
    ctx->DrawImage(gaussianBlurEffect, D2D1::Point2F(0.f, 0.f), rc);
}

void D2DUtil::drawText(RectF const& rc, std::wstring const& ws, d2d::Color const& color, Renderer::FontSelection font,
                       float size, DWRITE_TEXT_ALIGNMENT alignment, DWRITE_PARAGRAPH_ALIGNMENT verticalAlignment,
                       bool cache, bool hyphen, bool wrap) {
    ComPtr<IDWriteTextFormat> fmt = Necromancer::getRenderer().getTextFormat(font);
    brush->SetColor(color.get());
    auto layout = Necromancer::getRenderer().getLayout(fmt.Get(), ws, cache);
    if (layout.Get()) {
        layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        layout->SetMaxWidth(rc.getWidth());
        layout->SetMaxHeight(rc.getHeight());
        DWRITE_TEXT_RANGE range {};
        range.startPosition = 0;
        range.length = static_cast<UINT32>(ws.size());
        layout->SetFontSize(size, range);
        applySelectedTextDirection(layout.Get());
        layout->SetTextAlignment(alignment);
        layout->SetParagraphAlignment(verticalAlignment);
        if (Necromancer::getRenderer().isPixelFontActive()) {
            this->ctx->SetTextRenderingParams(Necromancer::getRenderer().getPixelFontParams());
        }
        this->ctx->DrawTextLayout({ rc.getPos().x, rc.getPos().y }, layout.Get(), brush);
        if (Necromancer::getRenderer().isPixelFontActive()) {
            this->ctx->SetTextRenderingParams(nullptr);
        }
#ifdef NECROMANCER_DEBUG
        if (Necromancer::get().shouldRenderDebugTextRects()) {
            DWRITE_TEXT_METRICS metrics {};
            layout->GetMetrics(&metrics);
            bool overflow = isDebugTextOverflow({ metrics.widthIncludingTrailingWhitespace, metrics.height }, rc);
            drawRectangle(rc, getDebugTextRectColor(overflow), overflow ? 1.5f : 1.f);
        }
#endif
    }
}

Vec2 D2DUtil::getTextSize(std::wstring const& ws, Renderer::FontSelection font, float size, bool tw, bool cache,
                          std::optional<Vec2> bounds, bool wrap) {
    ComPtr<IDWriteTextFormat> fmt = Necromancer::getRenderer().getTextFormat(font);
    auto ss = ctx->GetPixelSize();
    auto layout = Necromancer::getRenderer().getLayout(fmt.Get(), ws, cache);
    if (layout.Get()) {
        layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!bounds.has_value()) {
            layout->SetMaxWidth(static_cast<float>(ss.width));
            layout->SetMaxHeight(static_cast<float>(ss.height));
        } else {
            layout->SetMaxWidth(bounds->x);
            layout->SetMaxHeight(bounds->y);
        }
        DWRITE_TEXT_RANGE range;
        range.startPosition = 0;
        range.length = static_cast<UINT32>(ws.size());
        layout->SetFontSize(size, range);
        applySelectedTextDirection(layout.Get());
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        DWRITE_TEXT_METRICS textMetrics;
        DWRITE_OVERHANG_METRICS overhangs;
        layout->GetMetrics(&textMetrics);
        layout->GetOverhangMetrics(&overhangs);

        float width = tw ? textMetrics.widthIncludingTrailingWhitespace : textMetrics.width;
        float height = textMetrics.height;

        return Vec2(width, height);
    }
    return {};
}

void D2DUtil::drawTextClipped(RectF const& clipRect, RectF const& textRect, std::wstring const& text,
                              d2d::Color const& color, Renderer::FontSelection font, float size,
                              DWRITE_TEXT_ALIGNMENT alignment, DWRITE_PARAGRAPH_ALIGNMENT verticalAlignment,
                              bool cache, bool wrap) {
    ctx->PushAxisAlignedClip(clipRect.get(), D2D1_ANTIALIAS_MODE_ALIASED);
    drawText(textRect, text, color, font, size, alignment, verticalAlignment, cache, false, wrap);
    ctx->PopAxisAlignedClip();
}

void D2DUtil::drawWrappedTextClipped(RectF const& textRect, std::wstring const& text, d2d::Color const& color,
                                     Renderer::FontSelection font, float size, DWRITE_TEXT_ALIGNMENT alignment,
                                     DWRITE_PARAGRAPH_ALIGNMENT verticalAlignment, bool cache) {
    drawTextClipped(textRect, textRect, text, color, font, size, alignment, verticalAlignment, cache, true);
}

void D2DUtil::drawSingleLineFitted(RectF const& textRect, std::wstring const& text, d2d::Color const& color,
                                   Renderer::FontSelection font, float size, DWRITE_TEXT_ALIGNMENT alignment,
                                   DWRITE_PARAGRAPH_ALIGNMENT verticalAlignment, bool cache) {
    std::wstring fitted = ellipsizeToWidth(text, font, size, textRect.getWidth());
    drawTextClipped(textRect, textRect, fitted, color, font, size, alignment, verticalAlignment, cache);
}

float D2DUtil::getTextLineHeight(Renderer::FontSelection font, float size) {
    float measured = getTextSize(L"Ay", font, size, true, false, Vec2 { 10000.f, 10000.f }, false).y;
    return measured > 0.f ? measured : size * 1.25f;
}

float D2DUtil::getMeasuredTextHeight(RectF const& textRect, std::wstring const& text, Renderer::FontSelection font,
                                     float size, float maxLines) {
    if (textRect.getWidth() <= 0.f) return textRect.getHeight();

    float measured = getTextSize(text, font, size, true, false, Vec2 { textRect.getWidth(), 10000.f }, true).y;
    if (maxLines > 0.f) {
        measured = std::min(measured, getTextLineHeight(font, size) * maxLines);
    }

    return std::max(textRect.getHeight(), measured);
}

std::wstring D2DUtil::ellipsizeToWidth(std::wstring text, Renderer::FontSelection font, float size, float maxWidth) {
    text = singleLineText(std::move(text));
    if (text.empty() || maxWidth <= 1.f) return L"";

    auto textWidth = [&](std::wstring const& value) {
        return getTextSize(value, font, size, false, false, Vec2 { 10000.f, 10000.f }, false).x;
    };

    if (textWidth(text) <= maxWidth) return text;

    const std::wstring suffix = L"...";
    if (textWidth(suffix) > maxWidth) return L"";

    size_t lo = 0;
    size_t hi = text.size();
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (textWidth(text.substr(0, mid) + suffix) <= maxWidth) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }

    return text.substr(0, lo) + suffix;
}

float D2DUtil::fitTextSize(std::wstring const& text, Renderer::FontSelection font, RectF const& bounds, float maxSize,
                           float minSize) {
    std::wstring line = singleLineText(text);
    if (line.empty()) return maxSize;

    float availW = bounds.getWidth();
    float availH = bounds.getHeight();
    if (availW <= 1.f || availH <= 1.f) return minSize;
    if (minSize > maxSize) minSize = maxSize;

    auto measure = [&](float size) {
        return getTextSize(line, font, size, false, false, Vec2 { 100000.f, 100000.f }, false);
    };

    Vec2 at = measure(maxSize);
    if (at.x <= 0.f || at.y <= 0.f) return maxSize;
    if (at.x <= availW && at.y <= availH) return maxSize;

    // glyph metrics scale ~linearly with font size, so project the size that should fit
    float scale = std::min(availW / at.x, availH / at.y);
    float size = std::clamp(maxSize * scale, minSize, maxSize);

    // tighten in case the projection overshot (kerning/hinting is not perfectly linear)
    for (int i = 0; i < 6; i++) {
        Vec2 m = measure(size);
        if ((m.x <= availW && m.y <= availH) || size <= minSize + 0.01f) break;
        size = std::max(minSize, size * 0.94f);
    }

    return size;
}

void D2DUtil::drawAutoFitted(RectF const& textRect, std::wstring const& text, d2d::Color const& color,
                             Renderer::FontSelection font, float maxSize, DWRITE_TEXT_ALIGNMENT alignment,
                             DWRITE_PARAGRAPH_ALIGNMENT verticalAlignment, float minSize) {
    float size = fitTextSize(text, font, textRect, maxSize, minSize);
    // still clip + ellipsize as a last resort so nothing ever bleeds outside the rect
    drawSingleLineFitted(textRect, text, color, font, size, alignment, verticalAlignment, false);
}

d2d::Rect D2DUtil::getTextRect(std::wstring const& ws, Renderer::FontSelection font, float size, float pad,
                               bool cache) {
    ComPtr<IDWriteTextFormat> fmt = Necromancer::getRenderer().getTextFormat(font);
    auto ss = ctx->GetPixelSize();
    auto layout = Necromancer::getRenderer().getLayout(fmt.Get(), ws, cache);
    if (layout.Get()) {
        layout->SetMaxWidth(static_cast<float>(ss.width));
        layout->SetMaxHeight(static_cast<float>(ss.height));
        DWRITE_TEXT_RANGE range;
        range.startPosition = 0;
        range.length = static_cast<UINT32>(ws.size());
        layout->SetFontSize(size, range);
        applySelectedTextDirection(layout.Get());
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        DWRITE_TEXT_METRICS metrics;
        DWRITE_OVERHANG_METRICS overhangs;
        layout->GetMetrics(&metrics);
        layout->GetOverhangMetrics(&overhangs);

        float width =
            metrics.widthIncludingTrailingWhitespace + std::max(0.f, overhangs.left) + std::max(0.f, overhangs.right);
        float height = metrics.height + std::max(0.f, overhangs.top) + std::max(0.f, overhangs.bottom);
        return { -pad, -pad, width + pad, height + pad };
    }
    return {};
}

D2DUtil::D2DUtil()
    : brush(Necromancer::getRenderer().getSolidBrush())
    , ctx(Necromancer::getRenderer().getDeviceContext())
    , factory(Necromancer::getRenderer().getDWriteFactory()) {
}

SDK::RectangleArea MCDrawUtil::getRect(d2d::Rect const& rc) {
    return SDK::RectangleArea(rc.left * guiScale, rc.top * guiScale, rc.right * guiScale, rc.bottom * guiScale);
}

void MCDrawUtil::flush(bool text, bool mesh) {
    if (mesh) SDK::MeshHelpers::renderMeshImmediately(scn, scn->tess, SDK::MaterialPtr::getUIColor());
    if (text) renderCtx->flushText(0.f);
}

void MCDrawUtil::drawVignette(d2d::Color const& innerCol, float fade) {
    auto tess = scn->tess;
    *scn->shaderColor = innerCol;

    auto& ss = SDK::ClientInstance::get()->getGuiData()->guiSize;
    d2d::Rect scnRect = { 0.f, 0.f, ss.x, ss.y };
    auto center = scnRect.center();

    tess->begin(SDK::Primitive::Trianglestrip, 10);
    {
        tess->color({ 1.f, 1.f, 1.f, 1.f });
        tess->vertex(scnRect.right, scnRect.top);

        tess->color({ 1.f, 1.f, 1.f, fade });
        tess->vertex(center.x, center.y);
    }

    {
        tess->color({ 1.f, 1.f, 1.f, 1.f });
        tess->vertex(scnRect.right, scnRect.bottom);

        tess->color({ 1.f, 1.f, 1.f, fade });
        tess->vertex(center.x, center.y);
    }

    {
        tess->color({ 1.f, 1.f, 1.f, 1.f });
        tess->vertex(scnRect.left, scnRect.bottom);

        tess->color({ 1.f, 1.f, 1.f, fade });
        tess->vertex(center.x, center.y);
    }

    {
        tess->color({ 1.f, 1.f, 1.f, 1.f });
        tess->vertex(scnRect.left, scnRect.top);

        tess->color({ 1.f, 1.f, 1.f, fade });
        tess->vertex(center.x, center.y);
    }

    tess->color({ 1.f, 1.f, 1.f, 1.f });
    tess->vertex(scnRect.right, scnRect.top);

    flush(false);
}

void MCDrawUtil::drawImage(SDK::TexturePtr& texture, Vec2 const& pos, Vec2 const& size, d2d::Color const& flushCol) {
    this->renderCtx->drawImage(texture, { pos.x * this->guiScale, pos.y * guiScale },
                               { size.x * guiScale, size.y * guiScale }, { 0.f, 0.f }, { 1.f, 1.f });
    this->renderCtx->flushImages(flushCol, 1.f, SDK::HashedString("ui_textured_and_glcolor_sprite"));
}

void MCDrawUtil::drawImage(SDK::TexturePtr& texture, Vec2 const& pos, Vec2 const& size, Vec2 const& uvPos,
                           Vec2 const& uvSize, d2d::Color const& flushCol) {
    this->renderCtx->drawImage(texture, { pos.x * this->guiScale, pos.y * guiScale },
                               { size.x * guiScale, size.y * guiScale }, uvPos, uvSize);
    this->renderCtx->flushImages(flushCol, 1.f, SDK::HashedString("ui_textured_and_glcolor_sprite"));
}

d2d::Rect MCDrawUtil::drawItem(SDK::ItemStack* item, Vec2 const& pos, float sizeModifier, float opacity) {
    SDK::BaseActorRenderContext ctx =
        SDK::BaseActorRenderContext { this->renderCtx->screenContext, this->renderCtx->cinst,
                                      this->renderCtx->cinst->minecraftGame };

    auto it = item->getItem();

    ctx.itemRenderer->renderGuiItemNew(&ctx, item, 0, pos.x * guiScale, pos.y * guiScale, opacity,
                                       (sizeModifier * guiScale) * 3.f, 1.f, false);
    if (it && it->isGlint(item)) {
        ctx.itemRenderer->renderGuiItemNew(&ctx, item, 0, pos.x * guiScale, pos.y * guiScale, opacity,
                                           (sizeModifier * guiScale) * 3.f, 1.f, true);
    }

    constexpr float itemSize = 16.f;

    return { pos.x, pos.y, pos.x + ((itemSize)*sizeModifier) / guiScale, pos.y + ((itemSize)*sizeModifier) / guiScale };
}

bool MCDrawUtil::drawActor(SDK::Actor* actor, d2d::Rect const& bounds, float opacity) {
    if (!actor || !actor->aabbShape || !actor->stateVector || !renderCtx || !renderCtx->screenContext ||
        !renderCtx->cinst || !renderCtx->cinst->minecraftGame || !scn || !scn->matrix ||
        !Signatures::ActorRenderDispatcher_render.result) {
        return false;
    }

    SDK::BaseActorRenderContext ctx { renderCtx->screenContext, renderCtx->cinst, renderCtx->cinst->minecraftGame };
    auto& dispatcher = ctx.entityRenderDispatcher;
    if (!dispatcher) return false;

    float ownerWidth = bounds.getWidth() * guiScale;
    float ownerHeight = bounds.getHeight() * guiScale;
    constexpr float hudBodyYaw = -22.5f;
    constexpr float hudHeadYaw = -22.5f;
    constexpr float actorRenderDepth = -50.f;

    auto& matrixStack = scn->matrix->matrixStack;
    glm::mat4 parentTransform = matrixStack.empty() ? glm::mat4 { 1.f } : matrixStack.top();

    auto transformPoint = [](glm::mat4 const& matrix, Vec2 const& point) {
        glm::vec4 transformed = matrix * glm::vec4(point.x, point.y, 0.f, 1.f);
        return Vec2 {
            transformed.x,
            transformed.y,
        };
    };

    auto const& box = actor->getBoundingBox();
    Vec3 actorPos = actor->getPos();
    Vec3 localLower = box.lower - actorPos;
    Vec3 localHigher = box.higher - actorPos;
    Vec3 modelCenter = (localLower + localHigher) * 0.5f;
    float modelWidth = std::max(0.01f, std::max(localHigher.x - localLower.x, localHigher.z - localLower.z));
    float modelHeight = std::max(0.01f, localHigher.y - localLower.y);
    // aabb is gameplay size, so leave some room for hats, held items, and model layers
    modelWidth *= 1.16f;
    modelHeight *= 1.16f;

    float scale = std::min((ownerWidth * 0.82f) / modelWidth, (ownerHeight * 0.88f) / modelHeight);
    scale = std::min(scale, 96.f * guiScale);

    Vec2 frameCenter {
        (bounds.left * guiScale) + (ownerWidth * 0.5f),
        (bounds.top * guiScale) + (ownerHeight * 0.5f),
    };
    float anchorX = frameCenter.x - (modelCenter.x * scale);
    float anchorY = frameCenter.y + (modelCenter.y * scale);
    glm::mat4 localTransform { 1.f };
    localTransform =
        glm::translate(localTransform, glm::vec3(anchorX, anchorY, actorRenderDepth - (modelCenter.z * scale)));
    localTransform = glm::scale(localTransform, glm::vec3(-scale, scale, scale));
    localTransform = glm::rotate(localTransform, glm::radians(180.f), glm::vec3(0.f, 0.f, 1.f));
    if (actor->getStatusFlag(57)) {
        localTransform = glm::translate(localTransform, glm::vec3(0.f, 0.8f, 0.f));
    }
    glm::mat4 transform = parentTransform * localTransform;

    Vec2 clipTopLeft = transformPoint(parentTransform, { bounds.left * guiScale, bounds.top * guiScale });
    Vec2 clipBottomRight = transformPoint(parentTransform, { bounds.right * guiScale, bounds.bottom * guiScale });
    SDK::RectangleArea clip {
        std::min(clipTopLeft.x, clipBottomRight.x),
        std::min(clipTopLeft.y, clipBottomRight.y),
        std::max(clipTopLeft.x, clipBottomRight.x),
        std::max(clipTopLeft.y, clipBottomRight.y),
    };

    flush();
    renderCtx->saveCurrentClippingRectangle();
    renderCtx->setClippingRectangle(clip);
    matrixStack.push(transform);

    Vec3 cameraTarget {};
    Vec2 rotation {};
    struct ScopedActorHudPose {
        SDK::Actor* actor = nullptr;
        SDK::ActorRotationComponent* actorRotation = nullptr;
        SDK::ActorHeadRotationComponent* headRotation = nullptr;
        SDK::MobBodyRotationComponent* bodyRotation = nullptr;
        SDK::RenderRotationComponent* renderRotation = nullptr;
        Vec2 savedRotation {};
        Vec2 savedRotationOld {};
        SDK::RenderRotationComponent savedRenderRotation {};
        float savedHeadYaw = 0.f;
        float savedHeadYawOld = 0.f;
        float savedBodyYaw = 0.f;
        float savedBodyYawOld = 0.f;

        ScopedActorHudPose(SDK::Actor* actor, float forcedBodyYaw, float forcedHeadYaw)
            : actor(actor) {
            if (!actor) return;

            actorRotation = actor->actorRotation;
            bodyRotation = actor->tryGetComponent<SDK::MobBodyRotationComponent>();
            headRotation = actor->tryGetComponent<SDK::ActorHeadRotationComponent>();
            renderRotation = actor->tryGetComponent<SDK::RenderRotationComponent>();

            if (actorRotation) {
                savedRotation = actorRotation->rotation;
                savedRotationOld = actorRotation->rotationOld;
                actorRotation->rotation = { 0.f, forcedBodyYaw };
                actorRotation->rotationOld = { 0.f, forcedBodyYaw };
            }

            if (renderRotation) {
                savedRenderRotation = *renderRotation;
                renderRotation->rotation = { 0.f, forcedBodyYaw };
            }

            if (bodyRotation) {
                savedBodyYaw = bodyRotation->yBodyRot;
                savedBodyYawOld = bodyRotation->yBodyRotOld;
            }

            if (headRotation) {
                savedHeadYaw = headRotation->yHeadRot;
                savedHeadYawOld = headRotation->yHeadRotOld;
            }

            if (bodyRotation) {
                actor->setYBodyRotations(forcedBodyYaw, forcedBodyYaw);
            }
            if (headRotation) {
                actor->setYHeadRotations(forcedHeadYaw, forcedHeadYaw);
            }
        }

        ~ScopedActorHudPose() {
            if (!actor) return;

            if (bodyRotation) {
                actor->setYBodyRotations(savedBodyYaw, savedBodyYawOld);
            }
            if (headRotation) {
                actor->setYHeadRotations(savedHeadYaw, savedHeadYawOld);
            }
            if (actorRotation) {
                actorRotation->rotation = savedRotation;
                actorRotation->rotationOld = savedRotationOld;
            }
            if (renderRotation) *renderRotation = savedRenderRotation;
        }
    } poseOverride { actor, hudBodyYaw, hudHeadYaw };

    struct ScopedActorHudMolang {
        explicit ScopedActorHudMolang(SDK::Actor* actor) {
            if (!actor) return;

            actor->molangVariableMap.setMolangVariable(0x61917791E3C297E0ull, "variable.player_x_rotation", 0.f);
            actor->molangVariableMap.setMolangVariable(0xD921ED03129A7263ull, "variable.is_using_vr", 0.f);
            actor->molangVariableMap.setMolangVariable(0x2739F381184DE4AEull, "variable.is_first_person", 0.f);
        }
    } hudMolang { actor };

    struct ScopedActorUIRendering {
        SDK::Actor* actor = nullptr;
        bool changed = false;

        explicit ScopedActorUIRendering(SDK::Actor* actor)
            : actor(actor) {
            if (!actor) return;

            actor->setUIRendering(true);
            changed = true;
        }

        ~ScopedActorUIRendering() {
            if (changed && actor) {
                actor->setUIRendering(false);
            }
        }
    } uiRendering { actor };

    dispatcher->renderUI(&ctx, actor, cameraTarget, rotation, opacity > 0.f);

    matrixStack.pop();
    renderCtx->restoreSavedClippingRectangle();
    return true;
}

void MCDrawUtil::fillPolygon(Vec2 const& center, float radius, int numSides, d2d::Color const& col) {
    auto tess = scn->tess;
    float angle = (2.0f * pi_f) / static_cast<float>(numSides);

    tess->begin(SDK::Primitive::Trianglestrip, (numSides + 1) * 2);
    *scn->shaderColor = { 1.f, 1.f, 1.f, 1.f };
    tess->color(col);
    for (float i = 0; i <= static_cast<float>(numSides); ++i) {
        float x = center.x + (radius)*cos(i * angle);
        float y = center.y + (radius)*sin(i * angle);
        tess->vertex(x, y);
        tess->vertex(center.x, center.y);
    }

    flush(false);
}

void MCDrawUtil::drawPolygon(Vec2 const& center, float radius, int numSides, d2d::Color const& col,
                             float lineThickness) {
}

void MCDrawUtil::fillTriangle(Vec2 const& a, Vec2 const& b, Vec2 const& c, d2d::Color const& col) {
    auto tess = scn->tess;
    tess->begin(SDK::Primitive::Quad, 4);
    *scn->shaderColor = { 1.f, 1.f, 1.f, 1.f };
    tess->color(col);
    tess->vertex(a.x * guiScale, a.y * guiScale);
    tess->vertex(b.x * guiScale, b.y * guiScale);
    tess->vertex(c.x * guiScale, c.y * guiScale);
    tess->vertex(a.x * guiScale, a.y * guiScale);

    if (immediate) SDK::MeshHelpers::renderMeshImmediately(scn, tess, SDK::MaterialPtr::getUIColor());
}

void MCDrawUtil::fillRectangle(RectF const& rc, d2d::Color const& color) {
    auto rect = getRect(rc);
    auto tess = scn->tess;
    tess->begin(SDK::Primitive::Quad, 4);
    *scn->shaderColor = { 1.f, 1.f, 1.f, 1.f };
    tess->color(color);

    scn->tess->vertex(rect.left, rect.bottom);
    scn->tess->vertex(rect.right, rect.bottom);
    scn->tess->vertex(rect.right, rect.top);
    scn->tess->vertex(rect.left, rect.top);

    if (immediate) SDK::MeshHelpers::renderMeshImmediately(scn, tess, SDK::MaterialPtr::getUIColor());
}

void MCDrawUtil::drawRectangle(RectF const& rect, d2d::Color const& col, float lineThickness) {
    bool oImm = immediate;
    immediate = false;

    lineThickness /= 2.f;

    fillRectangle(
        { rect.left - lineThickness, rect.top - lineThickness, rect.right + lineThickness, rect.top + lineThickness },
        col); // left to right
    fillRectangle(
        { rect.left - lineThickness, rect.top - lineThickness, rect.left + lineThickness, rect.bottom + lineThickness },
        col); // left to bottom
    fillRectangle({ rect.right - lineThickness, rect.top - lineThickness, rect.right + lineThickness,
                    rect.bottom + lineThickness },
                  col); // right to bottom
    fillRectangle({ rect.left - lineThickness, rect.bottom - lineThickness, rect.right + lineThickness,
                    rect.bottom + lineThickness },
                  col); // bottomleft to bottomright
    immediate = oImm;
    if (immediate) {
        SDK::MeshHelpers::renderMeshImmediately(scn, scn->tess, SDK::MaterialPtr::getUIColor());
    }
}

void MCDrawUtil::fillRoundedRectangle(RectF const& rc, d2d::Color const& col, float radius) {
    auto rect = d2d::Rect(rc.left * guiScale, rc.top * guiScale, rc.right * guiScale, rc.bottom * guiScale);
    radius *= guiScale;
    int numSides = 20;
    auto tess = scn->tess;
    *scn->shaderColor = { 1.f, 1.f, 1.f, 1.f };

    tess->begin(SDK::Primitive::Trianglestrip, ((numSides * 2) * 4) + 2);
    tess->color(col);
    float angle = (2.0f * pi_f) / static_cast<float>(numSides);
    auto drawCorner = [tess, radius, angle, numSides, &rect](Vec2 const& center, float aMin, float aMax) {
        for (float i = 0; i <= static_cast<float>(numSides); ++i) {
            float myAngle = angle * i;
            if (myAngle < aMin || myAngle > aMax) continue;

            float x = center.x + (radius)*cos(myAngle);
            float y = center.y + (radius)*sin(myAngle);

            auto rc = rect.center();
            tess->vertex(x, y);
            tess->vertex(rc.x, rc.y);
        }
    };

    drawCorner({ rect.left + radius, rect.top + radius }, NecromancerMath::deg2rad(180.f), NecromancerMath::deg2rad(270.f));
    drawCorner({ rect.right - radius, rect.top + radius }, NecromancerMath::deg2rad(270.f), NecromancerMath::deg2rad(360.f));
    drawCorner({ rect.right - radius, rect.bottom - radius }, NecromancerMath::deg2rad(0.f), NecromancerMath::deg2rad(90.f));
    drawCorner({ rect.left + radius, rect.bottom - radius }, NecromancerMath::deg2rad(90.f), NecromancerMath::deg2rad(180.f));

    tess->vertex(rect.left, rect.top + radius);
    tess->vertex(rect.centerX(), rect.centerY());
    flush(false);
}

void MCDrawUtil::drawRoundedRectangle(RectF rect, d2d::Color const& color, float radius, float lineThickness,
                                      OutlinePosition outPos) {
    lineThickness *= guiScale;

    rect = d2d::Rect(rect.left * guiScale, rect.top * guiScale, rect.right * guiScale, rect.bottom * guiScale);
    radius *= guiScale;
    int numSides = 20;
    auto tess = scn->tess;
    *scn->shaderColor = { 1.f, 1.f, 1.f, 1.f };

    tess->begin(SDK::Primitive::Trianglestrip, ((numSides * 2) * 4) + 2);
    tess->color(color);
    float angle = (2.0f * pi_f) / static_cast<float>(numSides);
    auto drawCorner = [tess, radius, angle, numSides, &rect, lineThickness](Vec2 const& center, float aMin,
                                                                            float aMax) {
        for (float i = 0; i <= static_cast<float>(numSides); ++i) {
            float myAngle = angle * i;
            if (myAngle < aMin || myAngle > aMax) continue;

            float x = center.x + (radius)*cos(myAngle);
            float y = center.y + (radius)*sin(myAngle);

            float innerX = center.x + (radius - lineThickness) * std::cos(myAngle);
            float innerY = center.y + (radius - lineThickness) * std::sin(myAngle);

            tess->vertex(x, y);
            tess->vertex(innerX, innerY);
        }
    };

    drawCorner({ rect.left + radius, rect.top + radius }, NecromancerMath::deg2rad(180.f), NecromancerMath::deg2rad(270.f));
    drawCorner({ rect.right - radius, rect.top + radius }, NecromancerMath::deg2rad(270.f), NecromancerMath::deg2rad(360.f));
    drawCorner({ rect.right - radius, rect.bottom - radius }, NecromancerMath::deg2rad(0.f), NecromancerMath::deg2rad(90.f));
    drawCorner({ rect.left + radius, rect.bottom - radius }, NecromancerMath::deg2rad(90.f), NecromancerMath::deg2rad(180.f));

    tess->vertex(rect.left, rect.top + radius);
    tess->vertex(rect.left + lineThickness, rect.top + radius);
    flush(false);
}

void MCDrawUtil::drawText(RectF const& rc, std::wstring const& text, d2d::Color const& color,
                          Renderer::FontSelection fontSelection, float size, DWRITE_TEXT_ALIGNMENT alignment,
                          DWRITE_PARAGRAPH_ALIGNMENT verticalAlign, bool cache, bool hyphen, bool wrap) {
    if (!this->font) return;
    SDK::CaretMeasureData caretMeasure {};

    float newTop = rc.top;
    float height = size * (countof(text, L'\n') + 1);

    switch (verticalAlign) {
    case DWRITE_PARAGRAPH_ALIGNMENT_CENTER:
        newTop = rc.centerY(height * (10.f / this->font->getLineHeight()));
        break;
    case DWRITE_PARAGRAPH_ALIGNMENT_FAR:
        newTop = rc.bottom - (height);
        break;
    case DWRITE_PARAGRAPH_ALIGNMENT_NEAR:
        break;
    }

    RectF rMod = rc;
    rMod.top = newTop;
    renderCtx->drawText(this->font, getRect(rMod), util::WStrToStr(text), color, color.a,
                        (SDK::ui::TextAlignment)alignment,
                        SDK::TextMeasureData((size * guiScale) / this->font->getLineHeight(),
                                             Necromancer::get().shouldRenderTextShadows(), false),
                        caretMeasure);
#ifdef NECROMANCER_DEBUG
    if (Necromancer::get().shouldRenderDebugTextRects()) {
        Vec2 measured = getTextSize(text, fontSelection, size, true, false);
        bool overflow = isDebugTextOverflow(measured, rc);
        drawRectangle(rc, getDebugTextRectColor(overflow), overflow ? 1.5f : 1.f);
    }
#endif
}

Vec2 MCDrawUtil::getTextSize(std::wstring const& text, Renderer::FontSelection fontSelection, float size,
                             bool trailingWhitespace, bool cache, std::optional<Vec2> bounds, bool wrap) {
    float singleLineHeight = size;
    size_t lineBreaks = countof(text, L'\n');
    float totalHeight = (lineBreaks + 1) * singleLineHeight;

    if (!this->font) return { 0.f, totalHeight };

    float scale = (size * guiScale) / this->font->getLineHeight();

    if (lineBreaks == 0) {
        return { this->font->getLineLength(util::WStrToStr(text), scale, false) / guiScale, totalHeight };
    }

    float longest = 0.f;
    for (auto line : text | std::views::split(L'\n')) {
        std::wstring_view wline(line.begin(), line.end());
        auto sz = this->font->getLineLength(util::WStrToStr(std::wstring(wline)), scale, false) / guiScale;
        if (sz > longest) {
            longest = sz;
        }
    }

    return { longest, totalHeight };
}

DrawUtil::RectF MCDrawUtil::getTextRect(std::wstring const& text, Renderer::FontSelection fontSelection, float size, float pad,
                                        bool cache) {
    return {};
}
