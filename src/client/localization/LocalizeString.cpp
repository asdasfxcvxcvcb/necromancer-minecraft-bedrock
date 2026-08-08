#include "pch.h"
#include "LocalizeString.h"

LocalizedString LocalizeString::get(const std::string& id) {
    return { id, Necromancer::get().getL10nData().get(id) };
}
