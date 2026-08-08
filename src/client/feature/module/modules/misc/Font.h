#pragma once
#include "../../Module.h"

class Font : public Module {
public:
    Font();

    void onInit() override;

    bool isToggleable() override;
    bool showToggle() override { return false; }

private:
    static constexpr int font_client = 0;
    static constexpr int font_minecraft = 1;
    static constexpr int font_noto = 2;
    EnumData fontMode;
};
