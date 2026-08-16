#include "pch.h"
#include "I18n.h"

#include "mc/Addresses.h"

SDK::I18n* SDK::I18n::get() {
    if (!Signatures::I18n_getI18n.result) return nullptr;

    return reinterpret_cast<I18n*>(Signatures::I18n_getI18n.result);
}

std::string SDK::I18n::get(std::string const& key) {
    auto vtable = *reinterpret_cast<void***>(this);
    if (!vtable || !vtable[Signatures::VtableIndex::I18n::get]) return {};

    std::string result;
    std::shared_ptr<::Localization const> locale;
    using GetFunction =
        std::string*(__fastcall*)(I18n*, std::string*, std::string const*, std::shared_ptr<::Localization const>*);
    reinterpret_cast<GetFunction>(vtable[Signatures::VtableIndex::I18n::get])(this, &result, &key, &locale);
    return result;
}
