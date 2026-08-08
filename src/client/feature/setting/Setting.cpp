#include "pch.h"
#include "Setting.h"
#include "SettingGroup.h"

bool Setting::shouldRender(SettingGroup& group) {
    if (this->condition.empty()) return true;
    for (auto const& c : this->condition.conds) {
        bool matched = false;
        group.forEach([&](std::shared_ptr<Setting> set) {
            if (matched) return;
            if (set->name() != c.settingName) return;
            std::visit(
                [&](auto&& item) {
                    int val = item.getInt();
                    matched = std::find(c.values.begin(), c.values.end(), val) != c.values.end();
                },
                *set->value);
        });
        if (c.negate) matched = !matched;
        if (this->condition.isOr) {
            if (matched) return true;
        } else {
            if (!matched) return false;
        }
    }
    return !this->condition.isOr;
}

StoredColor ColorValue::getMainColor() const {
    if (!isRGB) {
        return color1;
    }
    auto color1HSV = util::ColorToHSV({ color1.r, color1.g, color1.b, color1.a });
    color1HSV.h = Necromancer::get().getRGBHue() * 360.f;
    auto col = util::HSVToColor(color1HSV);
    return { col.r, col.g, col.b, color1.a };
}
