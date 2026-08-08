#include "pch.h"
#include "Font.h"
#include "client/Necromancer.h"

Font::Font()
    : Module("Font", LocalizeString::get("client.module.font.name"),
             LocalizeString::get("client.module.font.desc"), GAME, nokeybind, false) {
}

void Font::onInit() {
    fontMode.addEntry({ font_client, LocalizeString::get("client.module.font.client.name"),
                        LocalizeString::get("client.module.font.client.desc") });
    fontMode.addEntry({ font_minecraft, LocalizeString::get("client.module.font.minecraft.name"),
                        LocalizeString::get("client.module.font.minecraft.desc") });
    fontMode.addEntry({ font_noto, LocalizeString::get("client.settings.mcRendererFont.notoSans.name"),
                        LocalizeString::get("client.settings.mcRendererFont.notoSans.desc") });

    std::get<EnumValue>(*fontMode.getValue()) = EnumValue(Necromancer::get().getHUDFontMode());

    auto set = addEnumSetting("fontMode", LocalizeString::get("client.module.font.fontMode.name"),
                              LocalizeString::get("client.module.font.fontMode.desc"), fontMode);
    set->callback = [this](Setting&) {
        Necromancer::get().setHUDFontMode(fontMode.getSelectedKey());
        Necromancer::get().applyHUDFontFamily();
    };

    Necromancer::get().setHUDFontMode(fontMode.getSelectedKey());
    Necromancer::get().applyHUDFontFamily();
}

bool Font::isToggleable() {
    return false;
}
