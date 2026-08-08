#pragma once
#include "client/event/Listener.h"
#include "client/event/Event.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>

struct PlayerTag {
    std::string name;
    int priority = 0;
    float r = 1.f, g = 1.f, b = 1.f;
    bool isDefault = false;
};

struct PlayerStats {
    int kills = 0;
    int deaths = 0;
    int encounters = 0;
    int64_t firstSeen = 0;
    int64_t lastSeen = 0;
};

class PlayerListManager : public Listener {
public:
    static PlayerListManager& get();

    std::vector<PlayerTag>& getTags() { return tags; }
    PlayerTag* findTag(std::string const& name);
    bool createTag(std::string name, int priority, float r, float g, float b);
    bool renameTag(std::string const& oldName, std::string newName);
    bool deleteTag(std::string const& name);
    void setTagPriority(std::string const& name, int priority);
    void setTagColor(std::string const& name, float r, float g, float b);

    std::vector<std::string> getPlayerTags(std::string const& player) const;
    void addTagToPlayer(std::string const& player, std::string const& tag);
    void removeTagFromPlayer(std::string const& player, std::string const& tag);
    bool hasAnyTag(std::string const& player) const;
    PlayerTag* getColorTag(std::string const& player);
    bool isFriend(std::string const& player) const;
    int getPriority(std::string const& player) const;

    PlayerStats& getStats(std::string const& player);
    const std::unordered_map<std::string, PlayerStats>& getAllStats() const { return stats; }

    std::vector<std::string> getKnownPlayers() const {
        std::vector<std::string> out;
        out.reserve(stats.size() + playerTags.size());
        for (auto& [name, st] : stats) out.push_back(name);
        for (auto& [name, vec] : playerTags) {
            if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
        }
        return out;
    }

    bool isOnline(std::string const& player) const;
    const std::vector<std::string>& getOnlinePlayers() const { return onlinePlayers; }

    void onTick(Event& ev);
    void onAttack(Event& evG);
    void onPacketReceive(Event& evG);
    void onLeaveGame(Event& ev);

    void save();
    void saveAsync();
    void load();
    void shutdown();

private:
    PlayerListManager();
    ~PlayerListManager();

    void ensureDefaults();
    void closeEncounter(std::string const& player, int64_t nowSec);
    void writerLoop();

    std::vector<PlayerTag> tags;
    std::unordered_map<std::string, std::vector<std::string>> playerTags;
    std::unordered_map<std::string, PlayerStats> stats;
    std::unordered_map<std::string, bool> present;
    std::vector<std::string> onlinePlayers;

    std::thread writer;
    std::mutex writerMutex;
    std::condition_variable writerCv;
    std::string pendingTagsData;
    std::string pendingStatsData;
    bool pendingWrite = false;
    bool writerStop = false;

    std::unordered_map<uint64_t, std::pair<std::string, std::chrono::system_clock::time_point>> attackedPlayers;
    std::string lastAttacker;
    std::chrono::system_clock::time_point lastHurtTime {};
    std::atomic<bool> statsDirty { false };
    std::chrono::steady_clock::time_point lastStatsFlush {};
};
