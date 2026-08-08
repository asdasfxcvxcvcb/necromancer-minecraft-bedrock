#pragma once
#include "util/DXUtil.h"
#include <dwrite_3.h>

class EmbeddedFontCollection final {
public:
    EmbeddedFontCollection() = default;
    EmbeddedFontCollection(EmbeddedFontCollection&) = delete;
    EmbeddedFontCollection(EmbeddedFontCollection&&) = delete;

    bool create(IDWriteFactory* factory);
    void release();

    [[nodiscard]] IDWriteFontCollection* get() const { return collection.Get(); }
    [[nodiscard]] bool isReady() const { return collection.Get() != nullptr; }
    [[nodiscard]] static const wchar_t* familyName() { return L"Minecraft"; }

private:
    ComPtr<IDWriteFactory5> factory5;
    ComPtr<IDWriteInMemoryFontFileLoader> loader;
    ComPtr<IDWriteFontCollection1> collection;
};
