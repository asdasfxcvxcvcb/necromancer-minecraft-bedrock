#include "pch.h"
#include "BowIndicator.h"

BowIndicator::BowIndicator()
    : TextModule("BowIndicator", LocalizeString::get("client.textmodule.bowIndicator.name"),
                 LocalizeString::get("client.textmodule.bowIndicator.desc"), HUD, 400.f, 0, true) {
}

BowIndicator::~BowIndicator() {
}

void BowIndicator::onInit() {
    TextModule::onInit();

    std::get<BoolValue>(customSize) = BoolValue(true);

    settings->forEach([](std::shared_ptr<Setting> s) {
        if (s->name() == "customSize") s->visible = false;
        else if (s->name() == "prefix") s->visible = false;
        else if (s->name() == "suffix") s->visible = false;
        else if (s->name() == "padX") s->visible = false;
        else if (s->name() == "padY") s->visible = false;
    });

    addSetting("visual", LocalizeString::get("client.textmodule.bowIndicator.visual.name"),
               LocalizeString::get("client.textmodule.bowIndicator.visual.desc"), this->visual);
    addSetting("onlyInUse", LocalizeString::get("client.textmodule.bowIndicator.onlyInUse.name"),
               LocalizeString::get("client.textmodule.bowIndicator.onlyInUse.desc"), this->onlyInUse);
    addSetting("horizontal", LocalizeString::get("client.textmodule.bowIndicator.horizontal.name"),
               LocalizeString::get("client.textmodule.bowIndicator.horizontal.desc"), this->horizontal,
               "visual"_istrue);
    addSetting("hideWhenCharged", LocalizeString::get("client.textmodule.bowIndicator.hideWhenCharged.name"),
               LocalizeString::get("client.textmodule.bowIndicator.hideWhenCharged.desc"), this->hideWhenCharged,
               "visual"_istrue);

    addSetting("fgCol", LocalizeString::get("client.textmodule.bowIndicator.fgCol.name"),
               LocalizeString::get("client.textmodule.bowIndicator.fgCol.desc"), this->indicatorCol2,
               "visual"_istrue);
    addSetting("bgCol", LocalizeString::get("client.textmodule.bowIndicator.bgCol.name"),
               LocalizeString::get("client.textmodule.bowIndicator.bgCol.desc"), this->indicatorCol,
               "visual"_istrue);

    addSliderSetting("size", LocalizeString::get("client.textmodule.bowIndicator.size.name"),
                     LocalizeString::get("client.textmodule.bowIndicator.size.desc"), indicatorSize, FloatValue(0.f),
                     FloatValue(200.f), FloatValue(2.5f), "visual"_istrue);
    addSliderSetting("width", LocalizeString::get("client.textmodule.bowIndicator.width.name"),
                     LocalizeString::get("client.textmodule.bowIndicator.width.desc"), indicatorWidth, FloatValue(0.f),
                     FloatValue(200.f), FloatValue(2.5f), "visual"_istrue);
    addSliderSetting("rad", LocalizeString::get("client.textmodule.bowIndicator.rad.name"),
                     LocalizeString::get("client.textmodule.bowIndicator.rad.desc"), indicatorRad, FloatValue(0.f),
                     FloatValue(5.f), FloatValue(1.f), "visual"_istrue);
    addSliderSetting("padding", LocalizeString::get("client.textmodule.bowIndicator.padding.name"),
                     LocalizeString::get("client.textmodule.bowIndicator.padding.desc"), padding, FloatValue(0.f),
                     FloatValue(20.f), FloatValue(1.f), "visual"_istrue);
}

void BowIndicator::render(DrawUtil& dc, bool isDefault, bool inEditor) {
    if (!std::get<BoolValue>(visual)) {
        TextModule::render(dc, isDefault, inEditor);
        return;
    }
    auto plr = SDK::ClientInstance::get()->getLocalPlayer();
    if (!plr) return;
    auto slot = plr->supplies->inventory->getItem(plr->supplies->selectedSlot);

    bool horiz = std::get<BoolValue>(horizontal);
    float wid = std::get<FloatValue>(indicatorWidth);
    float siz = std::get<FloatValue>(indicatorSize);

    d2d::Rect rc = { 0.f, 0.f, horiz ? siz : wid, horiz ? wid : siz };
    float rad = std::get<FloatValue>(indicatorRad) / 10.f * (std::min)(rc.getWidth(), rc.getHeight());

    rect.right = rect.left + rc.getWidth();
    rect.bottom = rect.top + rc.getHeight();

    if (auto percent = getBowCharge(slot)) {
        if (std::get<BoolValue>(hideWhenCharged) && percent > 0.95f) {
            return;
        }

        dc.fillRoundedRectangle(rc, std::get<ColorValue>(indicatorCol).getMainColor(), rad);

        d2d::Rect fillRc = rc;

        float pad = std::get<FloatValue>(padding);
        fillRc.left += pad;
        fillRc.top += pad;
        fillRc.right -= pad;
        fillRc.bottom -= pad;

        if (horiz) {
            fillRc.right = fillRc.left + fillRc.getWidth() * percent.value();
        } else {
            fillRc.top = fillRc.bottom - fillRc.getHeight() * percent.value();
        }
        dc.fillRoundedRectangle(fillRc, std::get<ColorValue>(indicatorCol2).getMainColor(), rad);
    }
}

std::wstringstream BowIndicator::text(bool, bool) {
    auto plr = SDK::ClientInstance::get()->getLocalPlayer();
    if (!plr) {
        std::wstringstream empty;
        return empty;
    }
    auto slot = plr->supplies->inventory->getItem(plr->supplies->selectedSlot);
    auto charge = getBowCharge(slot);

    if (std::get<BoolValue>(onlyInUse) && !charge.has_value()) {
        std::wstringstream empty;
        return empty;
    }

    std::wstringstream wss;
    wss << std::round(charge.value_or(0.f) * 100.f) << "%";
    return wss;
}

std::optional<float> BowIndicator::getBowCharge(SDK::ItemStack* slot) {
    if (!slot->item) return std::nullopt;
    auto item = *slot->item;

    const auto itemHash = static_cast<uint64_t>(item->id.hash);
    if (itemHash == "bow"_fnv64 || itemHash == "crossbow"_fnv64 || itemHash == "trident"_fnv64) {
        int useDur = SDK::ClientInstance::get()->getLocalPlayer()->getItemUseDuration();
        if (useDur) {
            float chargeTickSpeed = 20.f;
            if (itemHash == "crossbow"_fnv64) {
                chargeTickSpeed = 20;
            } else if (itemHash == "trident"_fnv64) {
                chargeTickSpeed = 10;
            }
            float diff = static_cast<float>(item->getMaxUseDuration(slot) - useDur);
            return (std::min)((std::max)(diff / chargeTickSpeed, 0.f), 1.f);
        }
    }
    return std::nullopt;
}
