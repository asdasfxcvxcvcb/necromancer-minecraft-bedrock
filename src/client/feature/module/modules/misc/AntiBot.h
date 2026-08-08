#pragma once
#include "../../Module.h"
#include "util/LMath.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace SDK {
    class Actor;
}

class AntiBot : public Module {
public:
    enum class Mode : int {
        Balanced = 0,
        Custom = 1
    };

    AntiBot();

    void onEnable() override;
    void onDisable() override;
    void onLeaveGame(Event&);
    void onTick(Event&);

    static bool isBot(SDK::Actor* entt);

private:
    struct Cache {
        SDK::Actor* localActor = nullptr;
        uint64_t localRuntimeId = 0;
        bool playerListAvailable = false;
        std::unordered_set<std::string> playerNames;
        std::unordered_set<std::string> foldedPlayerNames;
        std::unordered_set<std::string> duplicateNames;
        std::unordered_map<uint64_t, uint64_t> absentTicks;
    };

    struct ActorState {
        uint64_t lastSeenTick = 0;
        uint64_t absentSinceTick = 0;
        std::string name;
    };

    void rebuildCache();
    void clearCache();

    static std::string foldName(std::string const& name);
    static bool hasInvalidName(std::string const& name);
    static bool finiteVec3(Vec3 const& value);
    static bool finiteVec2(Vec2 const& value);

    ValueType mode = EnumValue(0);
    ValueType playerListCheck = BoolValue(true);
    ValueType duplicateNameCheck = BoolValue(true);
    ValueType hitboxCheck = BoolValue(true);
    ValueType invalidNameCheck = BoolValue(true);
    ValueType invalidDataCheck = BoolValue(true);
    ValueType invisibleCheck = BoolValue(false);
    ValueType armorCheck = BoolValue(false);
    ValueType playerListGrace = FloatValue(1.f);
    ValueType minimumActorAge = FloatValue(2.f);
    ValueType hitboxWidthMin = FloatValue(0.25f);
    ValueType hitboxWidthMax = FloatValue(1.2f);
    ValueType hitboxHeightMin = FloatValue(0.25f);
    ValueType hitboxHeightMax = FloatValue(2.5f);
    ValueType maximumHealth = FloatValue(2048.f);
    ValueType maximumVelocity = FloatValue(100.f);

    EnumData modeData;
    std::atomic<std::shared_ptr<const Cache>> cache;
    std::unordered_map<uint64_t, ActorState> actorStates;
    uint64_t tickCounter = 0;

    static AntiBot* instance;
};
