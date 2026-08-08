#pragma once
#include "../../Module.h"

class ESP : public Module {
public:
    ESP();

    void onRenderLevel(RenderLevelEvent& event);
    void onRenderLayer(RenderLayerEvent& event);
    void onRenderOverlay(class Event& event);
    void afterLoadConfig() override;

private:
    void renderEntityOverlay(class DrawUtil& dc, bool emitText, bool emitIcons);
    void renderProjectedBoxes(class DrawUtil& dc);

    ValueType players = BoolValue(true);
    ValueType mobs = BoolValue(false);
    ValueType itemEntities = BoolValue(false);
    ValueType others = BoolValue(false);

    ValueType targeted = BoolValue(false);
    ValueType targetedName = BoolValue(true);
    ValueType targetedNameColor = ColorValue(1.f, 0.35f, 0.35f, 1.f);
    ValueType targetedHealth = BoolValue(true);
    ValueType targetedIcons = BoolValue(true);

    ValueType hitbox = BoolValue(true);
    ValueType hitboxColor = ColorValue(1.f, 1.f, 1.f, 1.f);
    ValueType hitboxThickness = FloatValue(0.3f);

    ValueType lineOfSight = BoolValue(true);
    ValueType lineOfSightColor = ColorValue(0.f, 0.f, 1.f, 1.f);
    ValueType lineOfSightThickness = FloatValue(0.3f);

    ValueType nametag = BoolValue(false);
    EnumData namePos;
    ValueType nameSize = FloatValue(16.f);
    ValueType nameColor = ColorValue(1.f, 1.f, 1.f, 1.f);
    ValueType nametagBg = BoolValue(true);
    ValueType nametagBgColor = ColorValue(0.f, 0.f, 0.f, 0.45f);

    ValueType health = BoolValue(false);
    EnumData healthMode;
    EnumData healthPos;
    ValueType healthBarThickness = FloatValue(6.f);
    ValueType healthTextSize = FloatValue(14.f);
    ValueType healthBarOutline = BoolValue(false);
    ValueType healthBarOutlineColor = ColorValue(0.f, 0.f, 0.f, 1.f);
    ValueType healthTextOutline = BoolValue(false);
    ValueType healthTextOutlineColor = ColorValue(0.f, 0.f, 0.f, 1.f);

    ValueType items = BoolValue(false);
    EnumData itemsPos;
    ValueType itemsSize = FloatValue(20.f);
    ValueType itemsBg = BoolValue(false);
};
