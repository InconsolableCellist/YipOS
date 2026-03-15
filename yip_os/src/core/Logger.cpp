#include "Logger.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <filesystem>

namespace YipOS {

std::ofstream Logger::logFile_;
bool Logger::initialized_ = false;
Logger::Level Logger::minLevel_ = Logger::Level::INFO;
std::mutex Logger::mutex_;

void Logger::Init(const std::string& logDirPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return;

    namespace fs = std::filesystem;
    fs::create_directories(logDirPath);

    fs::path logFilePath = fs::path(logDirPath) / "yip_os.log";

    // Rotate old log
    if (fs::exists(logFilePath)) {
        fs::path oldPath = fs::path(logDirPath) / "yip_os_old.log";
        if (fs::exists(oldPath)) fs::remove(oldPath);
        fs::rename(logFilePath, oldPath);
    }

    logFile_.open(logFilePath, std::ios::out);
    if (logFile_.is_open()) {
        initialized_ = true;
        std::string sep(50, '-');
        logFile_ << sep << "\n";
        logFile_ << "YipOS log started at " << GetTimeString() << "\n";
        logFile_ << sep << "\n";
        logFile_.flush();
        std::cout << "Logger initialized: " << logFilePath.string() << std::endl;
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_ && logFile_.is_open()) {
        logFile_ << "Log ended at " << GetTimeString() << "\n";
        logFile_.close();
        initialized_ = false;
    }
}

void Logger::Log(Level level, const std::string& message) {
    if (level < minLevel_) return;

    std::lock_guard<std::mutex> lock(mutex_);
    std::string entry = GetTimeString() + " [" + GetLevelString(level) + "] " + message;

    if (initialized_ && logFile_.is_open()) {
        logFile_ << entry << "\n";
        logFile_.flush();
    }

    if (level >= Level::WARNING || !initialized_) {
        std::cerr << entry << std::endl;
    }
}

void Logger::Debug(const std::string& msg) { Log(Level::DEBUG, msg); }
void Logger::Info(const std::string& msg) { Log(Level::INFO, msg); }
void Logger::Warning(const std::string& msg) { Log(Level::WARNING, msg); }
void Logger::Error(const std::string& msg) { Log(Level::ERROR, msg); }
void Logger::Critical(const std::string& msg) { Log(Level::CRITICAL, msg); }

void Logger::SetLogLevel(Level level) {
    std::lock_guard<std::mutex> lock(mutex_);
    minLevel_ = level;
}

std::string Logger::GetTimeString() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::GetLevelString(Level level) {
    switch (level) {
        case Level::DEBUG:    return "DEBUG";
        case Level::INFO:     return "INFO";
        case Level::WARNING:  return "WARNING";
        case Level::ERROR:    return "ERROR";
        case Level::CRITICAL: return "CRITICAL";
        default:              return "UNKNOWN";
    }
}

Logger::Level Logger::StringToLevel(const std::string& str) {
    if (str == "DEBUG") return Level::DEBUG;
    if (str == "INFO") return Level::INFO;
    if (str == "WARNING") return Level::WARNING;
    if (str == "ERROR") return Level::ERROR;
    if (str == "CRITICAL") return Level::CRITICAL;
    return Level::INFO;
}

std::string Logger::LevelToString(Level level) {
    return GetLevelString(level);
}

} // namespace YipOS
