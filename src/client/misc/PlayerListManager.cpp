#include "pch.h"
#include "PlayerListManager.h"
#include "client/event/Eventing.h"
#include "client/misc/EntityCache.h"
#include "mc/common/world/actor/player/Player.h"
#include "mc/common/world/level/Level.h"
#include "mc/common/network/packet/ActorEventPacket.h"
#include <mutex>
#include <thread>

namespace {
    bool equalsCI(std::string const& a, std::string const& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++) {
            if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    }

    int64_t nowSeconds() {
        return static_cast<int64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    }

    std::filesystem::path tagsPath() { return util::GetNecromancerPath() / "playerlist.json"; }
    std::filesystem::path statsPath() { return util::GetNecromancerPath() / "stats.json"; }

    std::mutex saveIoMutex;

    void writePayloads(std::string tagsData, std::string statsData) {
        std::lock_guard lock { saveIoMutex };
        try {
            std::ofstream ofs(tagsPath(), std::ios::binary | std::ios::trunc);
            ofs << tagsData;
            std::ofstream ofs2(statsPath(), std::ios::binary | std::ios::trunc);
            ofs2 << statsData;
        } catch (...) {
        }
    }
}

PlayerListManager& PlayerListManager::get() {
    static auto* instance = new PlayerListManager;
    return *instance;
}

PlayerListManager::PlayerListManager() {
    load();
    writer = std::thread(&PlayerListManager::writerLoop, this);
    Eventing::get().listen<TickEvent>(this, (EventListenerFunc)&PlayerListManager::onTick);
    Eventing::get().listen<AttackEvent>(this, (EventListenerFunc)&PlayerListManager::onAttack);
    Eventing::get().listen<PacketReceiveEvent>(this, (EventListenerFunc)&PlayerListManager::onPacketReceive);
    Eventing::get().listen<LeaveGameEvent>(this, (EventListenerFunc)&PlayerListManager::onLeaveGame);
}

PlayerListManager::~PlayerListManager() {
}

void PlayerListManager::writerLoop() {
    std::unique_lock lock { writerMutex };
    while (!writerStop) {
        writerCv.wait(lock, [this] { return writerStop || pendingWrite; });
        if (writerStop) break;
        std::string tagsData = std::move(pendingTagsData);
        std::string statsData = std::move(pendingStatsData);
        pendingWrite = false;
        lock.unlock();
        writePayloads(tagsData, statsData);
        lock.lock();
    }
}

void PlayerListManager::shutdown() {
    {
        std::lock_guard lock { writerMutex };
        writerStop = true;
        pendingWrite = false;
        pendingTagsData.clear();
        pendingStatsData.clear();
    }
    writerCv.notify_all();
    if (writer.joinable()) writer.join();
}

PlayerTag* PlayerListManager::findTag(std::string const& name) {
    for (auto& tag : tags) {
        if (tag.name == name) return &tag;
    }
    return nullptr;
}

bool PlayerListManager::createTag(std::string name, int priority, float r, float g, float b) {
    if (name.empty() || equalsCI(name, "default")) return false;
    for (auto& tag : tags) {
        if (equalsCI(tag.name, name)) return false;
    }
    tags.push_back({ std::move(name), std::clamp(priority, -1, 10), r, g, b, false });
    save();
    return true;
}

bool PlayerListManager::renameTag(std::string const& oldName, std::string newName) {
    auto* tag = findTag(oldName);
    if (!tag || newName.empty()) return false;
    if (!tag->isDefault && equalsCI(newName, "default")) return false;
    for (auto& other : tags) {
        if (other.name != oldName && equalsCI(other.name, newName)) return false;
    }
    for (auto& [player, vec] : playerTags) {
        for (auto& entry : vec) {
            if (entry == oldName) entry = newName;
        }
    }
    tag->name = std::move(newName);
    save();
    return true;
}

bool PlayerListManager::deleteTag(std::string const& name) {
    auto* tag = findTag(name);
    if (!tag || tag->isDefault) return false;
    for (auto& [player, vec] : playerTags) {
        std::erase(vec, name);
    }
    std::erase_if(tags, [&](PlayerTag const& t) { return t.name == name; });
    save();
    return true;
}

void PlayerListManager::setTagPriority(std::string const& name, int priority) {
    auto* tag = findTag(name);
    if (!tag || tag->isDefault) return;
    tag->priority = std::clamp(priority, -1, 10);
}

void PlayerListManager::setTagColor(std::string const& name, float r, float g, float b) {
    auto* tag = findTag(name);
    if (!tag) return;
    tag->r = r;
    tag->g = g;
    tag->b = b;
}

std::vector<std::string> PlayerListManager::getPlayerTags(std::string const& player) const {
    auto it = playerTags.find(player);
    if (it == playerTags.end()) return {};
    return it->second;
}

void PlayerListManager::addTagToPlayer(std::string const& player, std::string const& tag) {
    if (!findTag(tag)) return;
    auto& vec = playerTags[player];
    std::erase(vec, tag);
    vec.push_back(tag);
    save();
}

void PlayerListManager::removeTagFromPlayer(std::string const& player, std::string const& tag) {
    auto it = playerTags.find(player);
    if (it == playerTags.end()) return;
    std::erase(it->second, tag);
    save();
}

bool PlayerListManager::hasAnyTag(std::string const& player) const {
    auto it = playerTags.find(player);
    return it != playerTags.end() && !it->second.empty();
}

PlayerTag* PlayerListManager::getColorTag(std::string const& player) {
    auto it = playerTags.find(player);
    if (it == playerTags.end() || it->second.empty()) return nullptr;
    return findTag(it->second.back());
}

bool PlayerListManager::isFriend(std::string const& player) const {
    auto it = playerTags.find(player);
    if (it == playerTags.end()) return false;
    for (auto& tagName : it->second) {
        for (auto& tag : tags) {
            if (tag.name == tagName && tag.priority < 0) return true;
        }
    }
    return false;
}

int PlayerListManager::getPriority(std::string const& player) const {
    auto it = playerTags.find(player);
    if (it == playerTags.end()) return 0;
    int best = 0;
    for (auto& tagName : it->second) {
        for (auto& tag : tags) {
            if (tag.name == tagName) best = std::max(best, tag.priority);
        }
    }
    return best;
}

PlayerStats& PlayerListManager::getStats(std::string const& player) {
    return stats[player];
}

bool PlayerListManager::isOnline(std::string const& player) const {
    return std::find(onlinePlayers.begin(), onlinePlayers.end(), player) != onlinePlayers.end();
}

void PlayerListManager::closeEncounter(std::string const& player, int64_t nowSec) {
    present[player] = false;
    auto& st = stats[player];
    st.encounters++;
    st.lastSeen = nowSec;
}

void PlayerListManager::onTick(Event&) {
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return;
    auto level = ci->minecraft->getLevel();
    if (!level) return;
    auto lp = ci->getLocalPlayer();
    if (!lp) return;
    auto list = level->getPlayerList();
    if (!list) return;

    auto& selfName = lp->playerName;
    std::vector<std::string> now;
    now.reserve(list->size());
    for (auto& pair : *list) {
        auto& name = pair.second.name;
        if (name.empty() || name == selfName) continue;
        now.push_back(name);
    }

    int64_t nowSec = nowSeconds();
    bool dirty = false;

    for (auto& name : now) {
        if (!present[name]) {
            present[name] = true;
            auto& st = stats[name];
            if (st.firstSeen == 0) {
                st.firstSeen = nowSec;
                dirty = true;
            }
        }
    }
    for (auto& [name, isPresent] : present) {
        if (isPresent && std::find(now.begin(), now.end(), name) == now.end()) {
            closeEncounter(name, nowSec);
            dirty = true;
        }
    }
    if (dirty) statsDirty.store(true, std::memory_order_release);

    onlinePlayers = std::move(now);

    auto steadyNow = std::chrono::steady_clock::now();
    if (statsDirty.load(std::memory_order_acquire) && steadyNow - lastStatsFlush > 30s) {
        lastStatsFlush = steadyNow;
        statsDirty.store(false, std::memory_order_release);
        saveAsync();
    }

    auto nowTp = std::chrono::system_clock::now();
    for (auto it = attackedPlayers.begin(); it != attackedPlayers.end();) {
        if (nowTp - it->second.second > 10s) it = attackedPlayers.erase(it);
        else ++it;
    }
    if (!lastAttacker.empty() && nowTp - lastHurtTime > 10s) lastAttacker.clear();
}

void PlayerListManager::onAttack(Event& evG) {
    auto& ev = reinterpret_cast<AttackEvent&>(evG);
    auto* actor = ev.getActor();
    if (!actor || !actor->isPlayer()) return;
    auto& name = reinterpret_cast<SDK::Player*>(actor)->playerName;
    if (name.empty()) return;
    attackedPlayers[actor->getRuntimeID()] = { name, std::chrono::system_clock::now() };
}

void PlayerListManager::onPacketReceive(Event& evG) {
    auto& ev = reinterpret_cast<PacketReceiveEvent&>(evG);
    auto* pkt = ev.getPacket();
    if (pkt->getID() != SDK::PacketID::ACTOR_EVENT) return;
    auto* ae = static_cast<SDK::ActorEventPacket*>(pkt);

    auto ci = SDK::ClientInstance::get();
    if (!ci) return;
    auto lp = ci->getLocalPlayer();
    if (!lp) return;

    if (ae->eventID == SDK::ActorEventID::HURT_ANIMATION && ae->runtimeID == lp->getRuntimeID()) {
        float bestDist = 7.f;
        std::string bestName;
        auto snap = EntityCache::get().snapshot();
        for (auto* actor : snap->actors) {
            if (!actor || actor == lp || !actor->isPlayer()) continue;
            float dist = actor->getPos().distance(lp->getPos());
            if (dist < bestDist) {
                auto& name = reinterpret_cast<SDK::Player*>(actor)->playerName;
                if (name.empty()) continue;
                bestDist = dist;
                bestName = name;
            }
        }
        if (!bestName.empty()) {
            lastAttacker = bestName;
            lastHurtTime = std::chrono::system_clock::now();
        }
        return;
    }

    if (ae->eventID != SDK::ActorEventID::DEATH_ANIMATION) return;

    if (ae->runtimeID == lp->getRuntimeID()) {
        if (!lastAttacker.empty()) {
            stats[lastAttacker].deaths++;
            lastAttacker.clear();
            statsDirty.store(true, std::memory_order_release);
        }
        return;
    }

    auto it = attackedPlayers.find(ae->runtimeID);
    if (it != attackedPlayers.end()) {
        stats[it->second.first].kills++;
        attackedPlayers.erase(it);
        statsDirty.store(true, std::memory_order_release);
    }
}

void PlayerListManager::onLeaveGame(Event&) {
    int64_t nowSec = nowSeconds();
    for (auto& [name, isPresent] : present) {
        if (isPresent) closeEncounter(name, nowSec);
    }
    onlinePlayers.clear();
    attackedPlayers.clear();
    lastAttacker.clear();
    save();
}

void PlayerListManager::ensureDefaults() {
    tags.clear();
    tags.push_back({ "Default", 0, 1.f, 1.f, 1.f, true });
    tags.push_back({ "Friend", -1, 0.25f, 1.f, 0.25f, false });
    tags.push_back({ "Target", 1, 1.f, 0.25f, 0.25f, false });
}

void PlayerListManager::save() {
    try {
        json j;
        j["tags"] = json::array();
        for (auto& tag : tags) {
            json t;
            t["name"] = tag.name;
            t["priority"] = tag.priority;
            t["r"] = tag.r;
            t["g"] = tag.g;
            t["b"] = tag.b;
            t["isDefault"] = tag.isDefault;
            j["tags"].push_back(t);
        }
        json players = json::object();
        for (auto& [player, vec] : playerTags) {
            if (!vec.empty()) players[player] = vec;
        }
        j["players"] = players;

        json s;
        json sp = json::object();
        for (auto& [player, st] : stats) {
            json e;
            e["kills"] = st.kills;
            e["deaths"] = st.deaths;
            e["encounters"] = st.encounters;
            e["firstSeen"] = st.firstSeen;
            e["lastSeen"] = st.lastSeen;
            sp[player] = e;
        }
        s["players"] = sp;

        writePayloads(j.dump(2), s.dump(2));
    } catch (...) {
    }
}

void PlayerListManager::saveAsync() {
    try {
        json j;
        j["tags"] = json::array();
        for (auto& tag : tags) {
            json t;
            t["name"] = tag.name;
            t["priority"] = tag.priority;
            t["r"] = tag.r;
            t["g"] = tag.g;
            t["b"] = tag.b;
            t["isDefault"] = tag.isDefault;
            j["tags"].push_back(t);
        }
        json players = json::object();
        for (auto& [player, vec] : playerTags) {
            if (!vec.empty()) players[player] = vec;
        }
        j["players"] = players;

        json s;
        json sp = json::object();
        for (auto& [player, st] : stats) {
            json e;
            e["kills"] = st.kills;
            e["deaths"] = st.deaths;
            e["encounters"] = st.encounters;
            e["firstSeen"] = st.firstSeen;
            e["lastSeen"] = st.lastSeen;
            sp[player] = e;
        }
        s["players"] = sp;

        {
            std::lock_guard lock { writerMutex };
            pendingTagsData = j.dump(2);
            pendingStatsData = s.dump(2);
            pendingWrite = true;
        }
        writerCv.notify_one();
    } catch (...) {
    }
}

void PlayerListManager::load() {
    try {
        std::ifstream ifs(tagsPath(), std::ios::binary);
        if (!ifs.fail()) {
            json j = json::parse(ifs, nullptr, false);
            if (j.is_object() && j["tags"].is_array()) {
                tags.clear();
                for (auto& t : j["tags"]) {
                    PlayerTag tag;
                    tag.name = t["name"].is_string() ? t["name"].get<std::string>() : "";
                    if (tag.name.empty()) continue;
                    tag.priority = t["priority"].is_number() ? std::clamp(t["priority"].get<int>(), -1, 10) : 0;
                    tag.r = t["r"].is_number() ? t["r"].get<float>() : 1.f;
                    tag.g = t["g"].is_number() ? t["g"].get<float>() : 1.f;
                    tag.b = t["b"].is_number() ? t["b"].get<float>() : 1.f;
                    tag.isDefault = t["isDefault"].is_boolean() ? t["isDefault"].get<bool>() : false;
                    tags.push_back(tag);
                }
                if (j["players"].is_object()) {
                    for (auto& [player, vec] : j["players"].items()) {
                        if (!vec.is_array()) continue;
                        std::vector<std::string> tagNames;
                        for (auto& tn : vec) {
                            if (tn.is_string() && findTag(tn.get<std::string>()))
                                tagNames.push_back(tn.get<std::string>());
                        }
                        if (!tagNames.empty()) playerTags[player] = tagNames;
                    }
                }
            }
        }
    } catch (...) {
    }

    bool hasDefault = false;
    for (auto& tag : tags) {
        if (tag.isDefault) hasDefault = true;
    }
    if (tags.empty()) ensureDefaults();
    else if (!hasDefault) tags.insert(tags.begin(), { "Default", 0, 1.f, 1.f, 1.f, true });

    try {
        std::ifstream ifs(statsPath(), std::ios::binary);
        if (!ifs.fail()) {
            json s = json::parse(ifs, nullptr, false);
            if (s.is_object() && s["players"].is_object()) {
                for (auto& [player, e] : s["players"].items()) {
                    PlayerStats st;
                    st.kills = e["kills"].is_number() ? e["kills"].get<int>() : 0;
                    st.deaths = e["deaths"].is_number() ? e["deaths"].get<int>() : 0;
                    st.encounters = e["encounters"].is_number() ? e["encounters"].get<int>() : 0;
                    st.firstSeen = e["firstSeen"].is_number() ? e["firstSeen"].get<int64_t>() : 0;
                    st.lastSeen = e["lastSeen"].is_number() ? e["lastSeen"].get<int64_t>() : 0;
                    stats[player] = st;
                }
            }
        }
    } catch (...) {
    }
}
