#pragma once
#include <client/feature/module/Module.h>

class Freelook : public Module {
public:
    Freelook();
    virtual ~Freelook() {};

    void onCameraUpdate(Event&);
    void onPerspective(Event&);

    void onEnable() override;
    void onDisable() override;

    Vec2 getPinnedRot() const { return lastRot; }
    void setPinnedRot(Vec2 const& rot) { lastRot = rot; }

private:
    Vec2 lastRot;
};
