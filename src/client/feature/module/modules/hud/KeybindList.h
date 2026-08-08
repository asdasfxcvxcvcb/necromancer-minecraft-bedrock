#pragma once
#include "../../HUDModule.h"

class KeybindList : public HUDModule {
public:
    KeybindList();

    void render(DrawUtil& dc, bool, bool inEditor) override;

private:
    ValueType textSize = FloatValue(18.f);
    ValueType padding = FloatValue(4.f);
    ValueType gap = FloatValue(2.f);
    ValueType showKey = BoolValue(true);
    ValueType background = BoolValue(true);
    ValueType animate = BoolValue(true);

    ValueType bgCol = ColorValue(0.f, 0.f, 0.f, 0.35f);
    ValueType textCol = ColorValue(1.f, 1.f, 1.f, 1.f);
    ValueType activeCol = ColorValue(0.35f, 1.f, 0.35f, 1.f);

    std::map<std::string, float> anims;
};
