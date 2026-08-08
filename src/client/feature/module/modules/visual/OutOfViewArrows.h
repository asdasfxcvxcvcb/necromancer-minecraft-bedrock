#pragma once
#include "../../Module.h"

class OutOfViewArrows : public Module {
public:
    OutOfViewArrows();

    void onRenderOverlay(class Event& event);
    void onRenderLayer(class RenderLayerEvent& event);

private:
    void drawArrows(class DrawUtil& dc);
    ValueType arrowSize = FloatValue(12.f);
    ValueType space = FloatValue(40.f);
    ValueType range = FloatValue(25.f);
    ValueType players = BoolValue(true);
    ValueType mobs = BoolValue(false);
    ValueType playerColor = ColorValue(1.f, 0.28f, 0.15f, 1.f);
    ValueType mobColor = ColorValue(0.3f, 0.9f, 0.3f, 1.f);
};
