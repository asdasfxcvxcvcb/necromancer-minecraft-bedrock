#include "pch.h"
#include "EmbeddedFontCollection.h"
#include "client/resource/Resource.h"

LOAD_RESOURCE(font_Minecraft_ttf);

bool EmbeddedFontCollection::create(IDWriteFactory* factory) {
    if (collection.Get()) return true;

    HRESULT hr = factory->QueryInterface(IID_PPV_ARGS(factory5.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = factory5->CreateInMemoryFontFileLoader(loader.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = factory5->RegisterFontFileLoader(loader.Get());
    if (FAILED(hr)) return false;

    auto resource = GET_RESOURCE(font_Minecraft_ttf);
    const void* data = resource.data();
    uint32_t size = static_cast<uint32_t>(resource.size());

    ComPtr<IDWriteFontFile> fontFile;
    hr = loader->CreateInMemoryFontFileReference(factory5.Get(), data, size, nullptr, fontFile.GetAddressOf());
    if (FAILED(hr)) {
        factory5->UnregisterFontFileLoader(loader.Get());
        return false;
    }

    ComPtr<IDWriteFontSetBuilder1> setBuilder;
    hr = factory5->CreateFontSetBuilder(setBuilder.GetAddressOf());
    if (FAILED(hr)) {
        factory5->UnregisterFontFileLoader(loader.Get());
        return false;
    }

    hr = setBuilder->AddFontFile(fontFile.Get());
    if (FAILED(hr)) {
        factory5->UnregisterFontFileLoader(loader.Get());
        return false;
    }

    ComPtr<IDWriteFontSet> fontSet;
    hr = setBuilder->CreateFontSet(fontSet.GetAddressOf());
    if (FAILED(hr)) {
        factory5->UnregisterFontFileLoader(loader.Get());
        return false;
    }

    hr = factory5->CreateFontCollectionFromFontSet(fontSet.Get(), collection.GetAddressOf());
    if (FAILED(hr)) {
        factory5->UnregisterFontFileLoader(loader.Get());
        return false;
    }

    return true;
}

void EmbeddedFontCollection::release() {
    collection = nullptr;
    if (loader.Get() && factory5.Get()) {
        factory5->UnregisterFontFileLoader(loader.Get());
    }
    loader = nullptr;
    factory5 = nullptr;
}
