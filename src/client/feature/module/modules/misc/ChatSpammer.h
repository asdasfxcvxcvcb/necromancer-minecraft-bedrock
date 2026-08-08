#pragma once
#include "client/feature/module/Module.h"
#include <deque>

class ChatSpammer : public Module {
public:
    ChatSpammer();

    void onEnable() override;
    void onDisable() override;
    void onTick(Event& ev);
    void onAttack(Event& evG);
    void onPacketReceive(Event& evG);

private:
    struct MessageFile {
        std::wstring fileName;
        std::vector<std::string> lines;
        std::filesystem::file_time_type lastWrite {};
        std::chrono::steady_clock::time_point nextCheck {};
        int nextIndex = 0;
        int lastRandom = -1;
    };

    struct PendingMessage {
        std::string text;
        std::chrono::system_clock::time_point sendAt;
    };

    ValueType chatSpammer = BoolValue(true);
    ValueType openMessages = ButtonValue();
    ValueType interval = FloatValue(1.f);
    ValueType randomize = BoolValue(false);
    ValueType killsay = BoolValue(false);
    ValueType openKillsay = ButtonValue();
    ValueType killsayDelay = FloatValue(0.5f);
    ValueType killsayQueue = BoolValue(true);
    ValueType onlyTagged = BoolValue(false);

    MessageFile spamFile { L"chatspammer.txt" };
    MessageFile killsayFile { L"killsay.txt" };

    std::chrono::system_clock::time_point lastSpam {};
    std::chrono::system_clock::time_point lastKillsaySend {};
    std::deque<PendingMessage> pendingKillsay;
    std::unordered_map<uint64_t, std::string> attackedIds;
    std::chrono::system_clock::time_point lastAttack {};

    void openFile(MessageFile& file);
    void ensureFile(MessageFile const& file);
    void reloadIfChanged(MessageFile& file);
    std::string pickLine(MessageFile& file);
    void sendChat(std::string const& msg);
};
