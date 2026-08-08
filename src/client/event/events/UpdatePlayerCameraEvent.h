#pragma once
class UpdatePlayerCameraEvent : public Event {
public:
    static const uint32_t hash = TOHASH(UpdatePlayerCameraEvent);

    void setViewAngles(Vec2 const& angles) { newViewAngles = angles; }
    void setViewAnglesPersistent(Vec2 const& angles) {
        newViewAngles = angles;
        persistent = true;
    }

    std::optional<Vec2> getNewRot() { return newViewAngles; }
    bool isPersistent() const { return persistent; }

private:
    std::optional<Vec2> newViewAngles;
    bool persistent = false;
};
