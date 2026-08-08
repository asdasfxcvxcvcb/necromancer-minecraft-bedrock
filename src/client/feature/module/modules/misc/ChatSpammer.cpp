#include "pch.h"
#include "ChatSpammer.h"
#include "client/misc/PlayerListManager.h"
#include <shellapi.h>
#include <random>
#include "mc/common/network/MinecraftPackets.h"
#include "mc/common/network/packet/TextPacket.h"
#include "mc/common/network/packet/ActorEventPacket.h"
#include "mc/common/world/actor/player/Player.h"

namespace {
    std::mt19937 rng { std::random_device {}() };

    const char* const exampleLines[] = {
        "haha loser I won",
        "this game is too easy",
        "I could never lose",
        "why are you still trying to play",
    };
}

ChatSpammer::ChatSpammer()
    : Module("ChatSpammer", LocalizeString::get("client.module.chat.name"),
             LocalizeString::get("client.module.chat.desc"), GAME, nokeybind) {
    addSetting("chatSpammer", LocalizeString::get("client.module.chat.chatSpammer.name"),
               LocalizeString::get("client.module.chat.chatSpammer.desc"), chatSpammer);
    auto openMsg = addSetting("openMessagesFile", LocalizeString::get("client.module.chat.openMessagesFile.name"),
                              LocalizeString::get("client.module.chat.openMessagesFile.desc"), openMessages,
                              "chatSpammer"_istrue);
    openMsg->callback = [this](Setting&) { openFile(spamFile); };

    addSliderSetting("interval", LocalizeString::get("client.module.chat.interval.name"),
                     LocalizeString::get("client.module.chat.interval.desc"), interval, FloatValue(0.1f),
                     FloatValue(10.f), FloatValue(0.1f), "chatSpammer"_istrue);
    addSetting("randomize", LocalizeString::get("client.module.chat.randomize.name"),
               LocalizeString::get("client.module.chat.randomize.desc"), randomize, "chatSpammer"_istrue);

    addSetting("killsay", LocalizeString::get("client.module.chat.killSay.name"),
               LocalizeString::get("client.module.chat.killSay.desc"), killsay);
    auto openKs = addSetting("openKillsayFile", LocalizeString::get("client.module.chat.openKillsayFile.name"),
                             LocalizeString::get("client.module.chat.openKillsayFile.desc"), openKillsay,
                             "killsay"_istrue);
    openKs->callback = [this](Setting&) { openFile(killsayFile); };

    addSliderSetting("killsayDelay", LocalizeString::get("client.module.chat.killsayDelay.name"),
                     LocalizeString::get("client.module.chat.killsayDelay.desc"), killsayDelay,
                     FloatValue(0.f), FloatValue(10.f), FloatValue(0.1f), "killsay"_istrue);
    addSetting("killsayQueue", LocalizeString::get("client.module.chat.killsayQueue.name"),
               LocalizeString::get("client.module.chat.killsayQueue.desc"), killsayQueue, "killsay"_istrue);
    addSetting("onlyTagged", LocalizeString::get("client.module.chat.onlyTagged.name"),
               LocalizeString::get("client.module.chat.onlyTagged.desc"), onlyTagged, "killsay"_istrue);

    this->listen<TickEvent>(&ChatSpammer::onTick);
    this->listen<AttackEvent>(&ChatSpammer::onAttack);
    this->listen<PacketReceiveEvent>(&ChatSpammer::onPacketReceive);
}

void ChatSpammer::onEnable() {
    ensureFile(spamFile);
    ensureFile(killsayFile);
    spamFile.lastWrite = {};
    killsayFile.lastWrite = {};
    reloadIfChanged(spamFile);
    reloadIfChanged(killsayFile);
    auto intervalMs = std::chrono::milliseconds(static_cast<int>(std::get<FloatValue>(interval) * 1000.f));
    lastSpam = std::chrono::system_clock::now() - intervalMs;
    pendingKillsay.clear();
    attackedIds.clear();
}

void ChatSpammer::onDisable() {
    pendingKillsay.clear();
    attackedIds.clear();
}

void ChatSpammer::onTick(Event&) {
    auto now = std::chrono::system_clock::now();

    if (std::get<BoolValue>(chatSpammer)) {
        reloadIfChanged(spamFile);
        auto intervalMs = std::chrono::milliseconds(static_cast<int>(std::get<FloatValue>(interval) * 1000.f));
        if (!spamFile.lines.empty() && now - lastSpam >= intervalMs) {
            sendChat(pickLine(spamFile));
            lastSpam = now;
        }
    }

    if (std::get<BoolValue>(killsay)) {
        reloadIfChanged(killsayFile);
        bool queueOn = std::get<BoolValue>(killsayQueue);
        while (!pendingKillsay.empty() && pendingKillsay.front().sendAt <= now) {
            sendChat(pendingKillsay.front().text);
            lastKillsaySend = now;
            pendingKillsay.pop_front();
            if (queueOn) break;
        }
    } else if (!pendingKillsay.empty()) {
        pendingKillsay.clear();
    }

    if (now - lastAttack > 10s && !attackedIds.empty()) attackedIds.clear();
}

void ChatSpammer::onAttack(Event& evG) {
    if (!std::get<BoolValue>(killsay)) return;
    auto& ev = reinterpret_cast<AttackEvent&>(evG);
    auto* actor = ev.getActor();
    if (!actor || !actor->isPlayer()) return;

    auto& name = reinterpret_cast<SDK::Player*>(actor)->playerName;
    if (name.empty()) return;
    attackedIds[actor->getRuntimeID()] = name;
    lastAttack = std::chrono::system_clock::now();
}

void ChatSpammer::onPacketReceive(Event& evG) {
    if (!std::get<BoolValue>(killsay)) return;
    auto& ev = reinterpret_cast<PacketReceiveEvent&>(evG);
    auto* pkt = ev.getPacket();

    if (pkt->getID() != SDK::PacketID::ACTOR_EVENT) return;
    auto* actorEvent = static_cast<SDK::ActorEventPacket*>(pkt);
    if (actorEvent->eventID != SDK::ActorEventID::DEATH_ANIMATION) return;
    auto attacked = attackedIds.find(actorEvent->runtimeID);
    if (attacked == attackedIds.end()) return;
    std::string victimName = attacked->second;
    attackedIds.erase(attacked);

    if (std::get<BoolValue>(onlyTagged) && !PlayerListManager::get().hasAnyTag(victimName)) return;

    auto now = std::chrono::system_clock::now();
    auto sendAt = now + std::chrono::milliseconds(static_cast<int>(std::get<FloatValue>(killsayDelay) * 1000.f));
    if (std::get<BoolValue>(killsayQueue)) {
        auto earliest = pendingKillsay.empty() ? lastKillsaySend + 1500ms : pendingKillsay.back().sendAt + 1500ms;
        sendAt = std::max(sendAt, earliest);
    }
    pendingKillsay.push_back({ pickLine(killsayFile), sendAt });
}

void ChatSpammer::openFile(MessageFile& file) {
    ensureFile(file);
    auto path = util::GetNecromancerPath() / "chat" / file.fileName;
    ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    file.lastWrite = {};
}

void ChatSpammer::ensureFile(MessageFile const& file) {
    auto dir = util::GetNecromancerPath() / "chat";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto path = dir / file.fileName;
    if (std::filesystem::exists(path, ec)) return;

    std::ofstream ofs(path, std::ios::binary);
    for (auto* line : exampleLines) {
        ofs << line << '\n';
    }
}

void ChatSpammer::reloadIfChanged(MessageFile& file) {
    // Hot-reload only needs to notice edits within a second or so; polling file
    // metadata every 20Hz tick is needless syscall traffic on the tick thread.
    auto steadyNow = std::chrono::steady_clock::now();
    if (steadyNow < file.nextCheck) return;
    file.nextCheck = steadyNow + std::chrono::seconds(1);

    auto path = util::GetNecromancerPath() / "chat" / file.fileName;
    std::error_code ec;
    auto writeTime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        ensureFile(file);
        writeTime = std::filesystem::last_write_time(path, ec);
        if (ec) return;
    }
    if (writeTime == file.lastWrite) return;
    file.lastWrite = writeTime;

    std::ifstream ifs(path, std::ios::binary);
    std::vector<std::string> loaded;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        auto end = line.find_last_not_of(" \t");
        line = line.substr(start, end - start + 1);
        if (line.size() > 200) line.resize(200);
        loaded.push_back(line);
    }
    file.lines = std::move(loaded);
    if (file.nextIndex >= static_cast<int>(file.lines.size())) file.nextIndex = 0;
    file.lastRandom = -1;
}

std::string ChatSpammer::pickLine(MessageFile& file) {
    if (file.lines.empty()) return "";

    if (std::get<BoolValue>(randomize)) {
        if (file.lines.size() == 1) return file.lines[0];
        std::uniform_int_distribution<int> dist(0, static_cast<int>(file.lines.size()) - 1);
        int idx = dist(rng);
        while (idx == file.lastRandom) {
            idx = dist(rng);
        }
        file.lastRandom = idx;
        return file.lines[idx];
    }

    auto& out = file.lines[file.nextIndex];
    file.nextIndex = (file.nextIndex + 1) % static_cast<int>(file.lines.size());
    return out;
}

void ChatSpammer::sendChat(std::string const& msg) {
    if (msg.empty()) return;
    auto lp = SDK::ClientInstance::get()->getLocalPlayer();
    if (!lp) return;

    auto pkt = SDK::MinecraftPackets::createPacket(SDK::PacketID::TEXT);
    auto* tp = reinterpret_cast<SDK::TextPacket*>(pkt.get());
    tp->chat(msg);
    lp->packetSender->sendToServer(pkt.get());
}
