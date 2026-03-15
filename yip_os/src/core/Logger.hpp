#pragma once

#include <string>
#include <fstream>
#include <mutex>

namespace YipOS {

class Logger {
public:
    enum class Level {
        DEBUG,
        INFO,
        WARNING,
        ERROR,
        CRITICAL
    };

    static void Init(const std::string& logDirPath);
    static void Shutdown();

    static void Log(Level level, const std::string& message);
    static void Debug(const std::string& message);
    static void Info(const std::string& message);
    static void Warning(const std::string& message);
    static void Error(const std::string& message);
    static void Critical(const std::string& message);

    static bool IsInitialized() { return initialized_; }
    static void SetLogLevel(Level level);
    static Level StringToLevel(const std::string& str);
    static std::string LevelToString(Level level);

private:
    static std::ofstream logFile_;
    static bool initialized_;
    static Level minLevel_;
    static std::mutex mutex_;
    static std::string GetTimeString();
    static std::string GetLevelString(Level level);
};

} // namespace YipOS
