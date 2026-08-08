#include "pch.h"
#include "AntiBot.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <mc/common/world/ItemStack.h>
#include <mc/common/world/actor/player/Player.h>
#include <mc/common/world/level/Level.h>

namespace {
    constexpr float ticksPerSecond = 20.f;
    constexpr uint64_t staleActorTicks = 200;
    constexpr size_t maximumPlayerNameBytes = 256;

    bool settingEnabled(ValueType const& value) {
        return std::get<BoolValue>(value).value;
    }
}

AntiBot* AntiBot::instance = nullptr;

AntiBot::AntiBot()
    : Module("AntiBot", LocalizeString::get("client.module.antiBot.name"),
             LocalizeString::get("client.module.antiBot.desc"), GAME) {
    modeData.addEntry(EnumEntry(static_cast<int>(Mode::Balanced),
                                LocalizeString::get("client.module.antiBot.mode.balanced.name"),
                                LocalizeString::get("client.module.antiBot.mode.balanced.desc")));
    modeData.addEntry(EnumEntry(static_cast<int>(Mode::Custom),
                                LocalizeString::get("client.module.antiBot.mode.custom.name"),
                                LocalizeString::get("client.module.antiBot.mode.custom.desc")));
    modeData.setSelectedKey(static_cast<int>(Mode::Balanced));
    addEnumSetting("mode", LocalizeString::get("client.module.antiBot.mode.name"),
                   LocalizeString::get("client.module.antiBot.mode.desc"), modeData)
        ->defaultValue = EnumValue(static_cast<int>(Mode::Balanced));

    Setting::Condition customMode("mode", { static_cast<int>(Mode::Custom) });
    addSetting("playerListCheck", LocalizeString::get("client.module.antiBot.playerListCheck.name"),
               LocalizeString::get("client.module.antiBot.playerListCheck.desc"), playerListCheck, customMode);
    addSetting("duplicateNameCheck", LocalizeString::get("client.module.antiBot.duplicateNameCheck.name"),
               LocalizeString::get("client.module.antiBot.duplicateNameCheck.desc"), duplicateNameCheck, customMode);
    addSetting("hitboxCheck", LocalizeString::get("client.module.antiBot.hitboxCheck.name"),
               LocalizeString::get("client.module.antiBot.hitboxCheck.desc"), hitboxCheck, customMode);
    addSetting("invalidNameCheck", LocalizeString::get("client.module.antiBot.invalidNameCheck.name"),
               LocalizeString::get("client.module.antiBot.invalidNameCheck.desc"), invalidNameCheck, customMode);
    addSetting("invalidDataCheck", LocalizeString::get("client.module.antiBot.invalidDataCheck.name"),
               LocalizeString::get("client.module.antiBot.invalidDataCheck.desc"), invalidDataCheck, customMode);
    addSetting("invisibleCheck", LocalizeString::get("client.module.antiBot.invisibleCheck.name"),
               LocalizeString::get("client.module.antiBot.invisibleCheck.desc"), invisibleCheck, customMode);
    addSetting("armorCheck", LocalizeString::get("client.module.antiBot.armorCheck.name"),
               LocalizeString::get("client.module.antiBot.armorCheck.desc"), armorCheck, customMode);

    Setting::Condition playerListOptions(std::vector<Setting::SingleCond> {
        { "mode", { static_cast<int>(Mode::Custom) }, false },
        { "playerListCheck", { 1 }, false },
    });
    addSliderSetting("playerListGrace", LocalizeString::get("client.module.antiBot.playerListGrace.name"),
                     LocalizeString::get("client.module.antiBot.playerListGrace.desc"), playerListGrace,
                     FloatValue(0.f), FloatValue(10.f), FloatValue(0.25f), playerListOptions);
    addSliderSetting("minimumActorAge", LocalizeString::get("client.module.antiBot.minimumActorAge.name"),
                     LocalizeString::get("client.module.antiBot.minimumActorAge.desc"), minimumActorAge,
                     FloatValue(0.f), FloatValue(10.f), FloatValue(0.25f), playerListOptions);

    Setting::Condition hitboxOptions(std::vector<Setting::SingleCond> {
        { "mode", { static_cast<int>(Mode::Custom) }, false },
        { "hitboxCheck", { 1 }, false },
    });
    addSliderSetting("hitboxWidthMin", LocalizeString::get("client.module.antiBot.hitboxWidthMin.name"),
                     LocalizeString::get("client.module.antiBot.hitboxWidthMin.desc"), hitboxWidthMin,
                     FloatValue(0.05f), FloatValue(1.f), FloatValue(0.05f), hitboxOptions);
    addSliderSetting("hitboxWidthMax", LocalizeString::get("client.module.antiBot.hitboxWidthMax.name"),
                     LocalizeString::get("client.module.antiBot.hitboxWidthMax.desc"), hitboxWidthMax,
                     FloatValue(0.5f), FloatValue(3.f), FloatValue(0.05f), hitboxOptions);
    addSliderSetting("hitboxHeightMin", LocalizeString::get("client.module.antiBot.hitboxHeightMin.name"),
                     LocalizeString::get("client.module.antiBot.hitboxHeightMin.desc"), hitboxHeightMin,
                     FloatValue(0.05f), FloatValue(2.f), FloatValue(0.05f), hitboxOptions);
    addSliderSetting("hitboxHeightMax", LocalizeString::get("client.module.antiBot.hitboxHeightMax.name"),
                     LocalizeString::get("client.module.antiBot.hitboxHeightMax.desc"), hitboxHeightMax,
                     FloatValue(1.f), FloatValue(4.f), FloatValue(0.05f), hitboxOptions);

    Setting::Condition dataOptions(std::vector<Setting::SingleCond> {
        { "mode", { static_cast<int>(Mode::Custom) }, false },
        { "invalidDataCheck", { 1 }, false },
    });
    addSliderSetting("maximumHealth", LocalizeString::get("client.module.antiBot.maximumHealth.name"),
                     LocalizeString::get("client.module.antiBot.maximumHealth.desc"), maximumHealth,
                     FloatValue(20.f), FloatValue(10000.f), FloatValue(20.f), dataOptions);
    addSliderSetting("maximumVelocity", LocalizeString::get("client.module.antiBot.maximumVelocity.name"),
                     LocalizeString::get("client.module.antiBot.maximumVelocity.desc"), maximumVelocity,
                     FloatValue(10.f), FloatValue(500.f), FloatValue(5.f), dataOptions);

    this->listen<TickEvent>(&AntiBot::onTick, false, 200);
    this->listen<LeaveGameEvent>(&AntiBot::onLeaveGame, true);
    cache.store(std::make_shared<const Cache>(), std::memory_order_release);
    instance = this;
}

void AntiBot::onEnable() {
    clearCache();
    rebuildCache();
}

void AntiBot::onDisable() {
    clearCache();
}

void AntiBot::onLeaveGame(Event&) {
    clearCache();
}

void AntiBot::onTick(Event&) {
    rebuildCache();
}

std::string AntiBot::foldName(std::string const& name) {
    std::string folded;
    folded.reserve(name.size());
    for (unsigned char character : name) {
        folded.push_back(character < 0x80 ? static_cast<char>(std::tolower(character)) : static_cast<char>(character));
    }
    return folded;
}

bool AntiBot::hasInvalidName(std::string const& name) {
    if (name.empty() || name.size() > maximumPlayerNameBytes) return true;

    for (size_t index = 0; index < name.size(); ++index) {
        unsigned char character = static_cast<unsigned char>(name[index]);
        if (character == 0xC2 && index + 1 < name.size() &&
            static_cast<unsigned char>(name[index + 1]) == 0xA7)
            return true;
        if (character == 0xA7 || character < 0x20 || character == 0x7F) return true;
    }

    return false;
}

bool AntiBot::finiteVec3(Vec3 const& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool AntiBot::finiteVec2(Vec2 const& value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

void AntiBot::clearCache() {
    actorStates.clear();
    tickCounter = 0;
    cache.store(std::make_shared<const Cache>(), std::memory_order_release);
}

void AntiBot::rebuildCache() {
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) {
        clearCache();
        return;
    }

    auto level = ci->minecraft->getLevel();
    if (!level) {
        clearCache();
        return;
    }

    ++tickCounter;
    auto next = std::make_shared<Cache>();
    next->localActor = ci->getLocalPlayer();
    next->localRuntimeId = next->localActor ? next->localActor->getRuntimeID() : 0;

    auto playerList = level->getPlayerList();
    next->playerListAvailable = playerList != nullptr;
    if (playerList) {
        next->playerNames.reserve(playerList->size());
        next->foldedPlayerNames.reserve(playerList->size());
        for (auto& [_, entry] : *playerList) {
            if (entry.name.empty()) continue;
            next->playerNames.insert(entry.name);
            next->foldedPlayerNames.insert(foldName(entry.name));
        }
    }

    std::vector<SDK::Actor*> actors;
    level->getRuntimeActorList(actors);
    std::unordered_map<std::string, size_t> nameCounts;
    nameCounts.reserve(actors.size());

    for (auto* actor : actors) {
        if (!actor || !actor->isPlayer()) continue;
        uint64_t runtimeId = actor->getRuntimeID();
        if (runtimeId == 0) continue;

        auto* player = reinterpret_cast<SDK::Player*>(actor);
        std::string foldedName = foldName(player->playerName);
        auto [stateIt, inserted] = actorStates.try_emplace(runtimeId);
        auto& state = stateIt->second;
        if (inserted || state.name != foldedName) {
            state.absentSinceTick = 0;
            state.name = foldedName;
        }
        state.lastSeenTick = tickCounter;

        bool listed = next->playerNames.contains(player->playerName) || next->foldedPlayerNames.contains(foldedName);
        if (listed || !next->playerListAvailable) {
            state.absentSinceTick = 0;
        } else if (state.absentSinceTick == 0) {
            state.absentSinceTick = tickCounter;
        }

        if (state.absentSinceTick != 0) {
            next->absentTicks.emplace(runtimeId, tickCounter - state.absentSinceTick + 1);
        }
        if (actor != next->localActor && runtimeId != next->localRuntimeId && !foldedName.empty()) {
            ++nameCounts[foldedName];
        }
    }

    for (auto const& [name, count] : nameCounts) {
        if (count > 1 && !next->foldedPlayerNames.contains(name)) next->duplicateNames.insert(name);
    }

    for (auto it = actorStates.begin(); it != actorStates.end();) {
        if (tickCounter - it->second.lastSeenTick > staleActorTicks)
            it = actorStates.erase(it);
        else
            ++it;
    }

    cache.store(std::shared_ptr<const Cache>(std::move(next)), std::memory_order_release);
}

bool AntiBot::isBot(SDK::Actor* entt) {
    auto* mod = instance;
    if (!mod || !mod->isEnabled() || !entt || !entt->isPlayer()) return false;

    auto current = mod->cache.load(std::memory_order_acquire);
    if (!current) return false;

    uint64_t runtimeId = entt->getRuntimeID();
    if (entt == current->localActor || (runtimeId != 0 && runtimeId == current->localRuntimeId)) return false;

    bool balanced = std::get<EnumValue>(mod->mode).val == static_cast<int>(Mode::Balanced);
    bool doPlayerList = balanced || settingEnabled(mod->playerListCheck);
    bool doDuplicates = balanced || settingEnabled(mod->duplicateNameCheck);
    bool doHitbox = balanced || settingEnabled(mod->hitboxCheck);
    bool doInvalidName = balanced || settingEnabled(mod->invalidNameCheck);
    bool doInvalidData = balanced || settingEnabled(mod->invalidDataCheck);
    bool doInvisible = !balanced && settingEnabled(mod->invisibleCheck);
    bool doArmor = !balanced && settingEnabled(mod->armorCheck);

    auto* player = reinterpret_cast<SDK::Player*>(entt);
    auto const& name = player->playerName;
    std::string foldedName = foldName(name);

    if (doInvalidName && hasInvalidName(name)) return true;
    if (doDuplicates && !foldedName.empty() && current->duplicateNames.contains(foldedName)) return true;

    if (doPlayerList && current->playerListAvailable) {
        bool listed = current->playerNames.contains(name) || current->foldedPlayerNames.contains(foldedName);
        if (!listed) {
            float graceSeconds = balanced ? 1.f : std::get<FloatValue>(mod->playerListGrace).value;
            float ageSeconds = balanced ? 2.f : std::get<FloatValue>(mod->minimumActorAge).value;
            uint64_t graceTicks = static_cast<uint64_t>(std::ceil(std::max(0.f, graceSeconds) * ticksPerSecond));
            uint64_t minimumTicks = static_cast<uint64_t>(std::ceil(std::max(0.f, ageSeconds) * ticksPerSecond));
            uint64_t absentTicks = 0;
            if (auto it = current->absentTicks.find(runtimeId); it != current->absentTicks.end()) absentTicks = it->second;
            uint64_t actorTicks = entt->ticksExisted > 0 ? static_cast<uint64_t>(entt->ticksExisted) : 0;
            if (absentTicks > graceTicks && actorTicks >= minimumTicks) return true;
        }
    }

    if (doHitbox) {
        if (!entt->aabbShape) return true;
        Vec2 const& shapeSize = entt->aabbShape->size;
        float width = shapeSize.x;
        float height = shapeSize.y;
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.f || height <= 0.f) {
            AABB const& bounds = entt->aabbShape->boundingBox;
            if (!finiteVec3(bounds.lower) || !finiteVec3(bounds.higher)) return true;
            width = std::max(bounds.higher.x - bounds.lower.x, bounds.higher.z - bounds.lower.z);
            height = bounds.higher.y - bounds.lower.y;
        }
        float minimumWidth = balanced ? 0.25f : std::get<FloatValue>(mod->hitboxWidthMin).value;
        float maximumWidth = balanced ? 1.2f : std::get<FloatValue>(mod->hitboxWidthMax).value;
        float minimumHeight = balanced ? 0.25f : std::get<FloatValue>(mod->hitboxHeightMin).value;
        float maximumHeight = balanced ? 2.5f : std::get<FloatValue>(mod->hitboxHeightMax).value;
        if (!std::isfinite(width) || !std::isfinite(height) || minimumWidth > maximumWidth ||
            minimumHeight > maximumHeight || width < minimumWidth || width > maximumWidth ||
            height < minimumHeight || height > maximumHeight)
            return true;
    }

    if (doInvalidData) {
        if (runtimeId == 0 || entt->ticksExisted < 0) return true;
        if (entt->stateVector) {
            if (!finiteVec3(entt->stateVector->pos) || !finiteVec3(entt->stateVector->posOld) ||
                !finiteVec3(entt->stateVector->velocity))
                return true;
            float maximumSpeed = balanced ? 100.f : std::get<FloatValue>(mod->maximumVelocity).value;
            if (entt->stateVector->velocity.magnitudeSq() > maximumSpeed * maximumSpeed) return true;
        }
        if (entt->actorRotation && !finiteVec2(entt->actorRotation->rotation)) return true;
        auto health = entt->getHealth();
        auto maxHealth = entt->getMaxHealth();
        float healthLimit = balanced ? 2048.f : std::get<FloatValue>(mod->maximumHealth).value;
        if ((health && (!std::isfinite(*health) || *health < 0.f || *health > healthLimit)) ||
            (maxHealth && (!std::isfinite(*maxHealth) || *maxHealth <= 0.f || *maxHealth > healthLimit)) ||
            (health && maxHealth && *health > *maxHealth + 0.01f))
            return true;
    }

    if (doInvisible && entt->isInvisible()) return true;

    if (doArmor) {
        bool hasArmor = false;
        for (int slot = 0; slot < 4; ++slot) {
            auto* armor = entt->getArmor(slot);
            if (armor && armor->getItem()) {
                hasArmor = true;
                break;
            }
        }
        if (!hasArmor) return true;
    }

    return false;
}
