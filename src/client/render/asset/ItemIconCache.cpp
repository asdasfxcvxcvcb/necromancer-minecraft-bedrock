#include "pch.h"
#include "ItemIconCache.h"
#include "ItemIconRegistry.h"
#include "client/Necromancer.h"
#include "client/render/Renderer.h"
#include "mc/common/world/Item.h"
#include "mc/common/world/ItemStack.h"
#include "util/DrawContext.h"

#include <Shlwapi.h>
#include <limits>

using Microsoft::WRL::ComPtr;

namespace {
    const ItemIconRegistry::Entry* findEntry(std::string const& id) {
        auto entries = ItemIconRegistry::entries();
        size_t lo = 0;
        size_t hi = entries.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            int cmp = id.compare(entries[mid].id);
            if (cmp == 0) return &entries[mid];
            if (cmp < 0) hi = mid;
            else lo = mid + 1;
        }
        return nullptr;
    }
}

ItemIconCache& ItemIconCache::get() {
    static ItemIconCache instance;
    return instance;
}

void ItemIconCache::invalidate() {
    cache.clear();
    ownerCtx = nullptr;
}

ID2D1Bitmap* ItemIconCache::getBitmap(std::string const& itemId) {
    if (itemId.empty()) return nullptr;

    auto* dc = Necromancer::getRenderer().getDeviceContext();
    auto* factory = Necromancer::getRenderer().getImagingFactory();
    if (!dc || !factory) return nullptr;

    if (ownerCtx != dc) {
        cache.clear();
        ownerCtx = dc;
    }

    if (auto it = cache.find(itemId); it != cache.end()) {
        return it->second.Get();
    }

    const ItemIconRegistry::Entry* entry = findEntry(itemId);
    if (!entry) {
        cache.emplace(itemId, nullptr);
        return nullptr;
    }

    const size_t size = static_cast<size_t>(entry->end - entry->begin);
    if (size == 0 || size > (std::numeric_limits<UINT>::max)()) {
        cache.emplace(itemId, nullptr);
        return nullptr;
    }

    ComPtr<IStream> stream(
        SHCreateMemStream(reinterpret_cast<const BYTE*>(entry->begin), static_cast<UINT>(size)));
    if (!stream) {
        cache.emplace(itemId, nullptr);
        return nullptr;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                                decoder.GetAddressOf())) ||
        !decoder) {
        cache.emplace(itemId, nullptr);
        return nullptr;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || !frame) {
        cache.emplace(itemId, nullptr);
        return nullptr;
    }

    ComPtr<IWICFormatConverter> conv;
    if (FAILED(factory->CreateFormatConverter(conv.GetAddressOf())) || !conv) {
        cache.emplace(itemId, nullptr);
        return nullptr;
    }

    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom))) {
        cache.emplace(itemId, nullptr);
        return nullptr;
    }

    ComPtr<ID2D1Bitmap> bitmap;
    if (FAILED(dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr, bitmap.GetAddressOf()))) {
        cache.emplace(itemId, nullptr);
        return nullptr;
    }

    auto [it, _] = cache.emplace(itemId, bitmap);
    return it->second.Get();
}

bool ItemIconCache::drawItem(DrawUtil& dc, SDK::ItemStack* stack, Vec2 const& pos, float size) {
    if (!stack) return false;

    SDK::Item* raw = stack->getItem();
    if (!raw) return false;

    if (dc.isMinecraft()) {
        auto& mcDc = static_cast<MCDrawUtil&>(dc);
        if (!mcDc.scn || !mcDc.renderCtx) return false;
        mcDc.drawItem(stack, pos, size / 48.f, 1.f);
        return true;
    }

    auto& d2dDc = static_cast<D2DUtil&>(dc);
    if (!d2dDc.ctx) return false;

    ID2D1Bitmap* bitmap = get().getBitmap(raw->id.getString());
    if (!bitmap) return false;

    D2D1_RECT_F dest = D2D1::RectF(pos.x, pos.y, pos.x + size, pos.y + size);
    d2dDc.ctx->DrawBitmap(bitmap, dest, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    return true;
}
