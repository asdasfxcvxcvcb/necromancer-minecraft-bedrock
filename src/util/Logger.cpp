#include "pch.h"
#include "Logger.h"
#include "util/Util.h"
#include <ctime>
#include <mutex>

#include "client/Necromancer.h"
#include "client/misc/ClientMessageQueue.h"

namespace {
    std::mutex logMutex;
    std::ofstream latestStream;
    std::ofstream archiveStream;
    std::string archiveDate;
    bool logReady = false;

    void openStreams(std::string const& date) {
        auto path = util::GetNecromancerPath() / "Logs";

        if (!latestStream.is_open()) {
            latestStream.open(path / "latest.log", std::ios::out | std::ios::app);
        }

        if (archiveDate != date) {
            if (archiveStream.is_open()) archiveStream.close();
            archiveStream.open(path / ("Necromancer-" + date + ".log"), std::ios::out | std::ios::app);
            archiveDate = date;
        }
    }
}

void Logger::Setup() {
    auto path = util::GetNecromancerPath();
    std::error_code ec;
    std::filesystem::create_directories(path / "Logs", ec);

    std::lock_guard lock { logMutex };
    if (latestStream.is_open()) latestStream.close();
    if (archiveStream.is_open()) archiveStream.close();
    archiveDate.clear();

    latestStream.open(path / "Logs" / "latest.log", std::ios::out | std::ios::trunc);
    logReady = true;
}

void Logger::Shutdown() {
    std::lock_guard lock { logMutex };
    logReady = false;
    if (latestStream.is_open()) latestStream.close();
    if (archiveStream.is_open()) archiveStream.close();
    archiveDate.clear();
}

void Logger::LogInternal(Level level, std::string str) {
    str = util::RedactPrivatePaths(std::move(str));

    std::time_t t = std::time(0);
    std::tm now;
    localtime_s(&now, &t);

    char dateBuf[16];
    std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);

    char stampBuf[64];
    std::snprintf(stampBuf, sizeof(stampBuf), "[%d-%d-%d, %02d:%02d:%02d]", now.tm_mon + 1, now.tm_mday,
                  now.tm_year + 1900, now.tm_hour, now.tm_min, now.tm_sec);

    char const* prefixText = "INFO";
    switch (level) {
    case Level::Info: prefixText = "INFO"; break;
    case Level::Warn: prefixText = "WARN"; break;
    case Level::Fatal: prefixText = "FATAL"; break;
    }

    std::string pref = std::string(stampBuf) + " [" + prefixText + "] ";
    std::string mstr = pref + str + "\n";

    {
        std::lock_guard lock { logMutex };
        if (logReady) {
            openStreams(dateBuf);

            if (latestStream.is_open()) {
                latestStream << mstr;
                latestStream.flush();
            }
            if (archiveStream.is_open()) {
                archiveStream << mstr;
                archiveStream.flush();
            }
        }
    }

#if NECROMANCER_DEBUG
    OutputDebugStringA(mstr.c_str());
    Necromancer::get().getClientMessageQueue().push(util::Format("&7" + pref + "&r" + str));
#endif
}
