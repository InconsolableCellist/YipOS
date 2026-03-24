#include "Logger.hpp"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <filesystem>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define posix_open  _open
#define posix_write _write
#define posix_close _close
#define posix_fsync _commit
#define O_FLAGS (O_WRONLY | O_CREAT | O_TRUNC)
#else
#include <unistd.h>
#include <fcntl.h>
#define posix_open  ::open
#define posix_write ::write
#define posix_close ::close
#define posix_fsync ::fsync
#define O_FLAGS (O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC)
#endif

namespace YipOS {

int Logger::logFd_ = -1;
std::string Logger::logPath_;
bool Logger::initialized_ = false;
Logger::Level Logger::minLevel_ = Logger::Level::INFO;
std::mutex Logger::mutex_;

void Logger::WriteRaw(const char* data, size_t len) {
    if (logFd_ < 0) return;
    size_t written = 0;
    while (written < len) {
        auto n = posix_write(logFd_, data + written, static_cast<unsigned int>(len - written));
        if (n <= 0) break;
        written += static_cast<size_t>(n);
    }
}

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

    logPath_ = logFilePath.string();
    logFd_ = posix_open(logPath_.c_str(), O_FLAGS, 0644);
    if (logFd_ >= 0) {
        initialized_ = true;
        std::string sep(50, '-');
        std::string ts = GetTimeString();
        std::string header = sep + "\nYipOS log started at " + ts + "\n" + sep + "\n";
        WriteRaw(header.c_str(), header.size());
        posix_fsync(logFd_);
        fprintf(stdout, "Logger initialized (fd %d): %s\n", logFd_, logFilePath.string().c_str());
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_ && logFd_ >= 0) {
        std::string msg = "Log ended at " + GetTimeString() + "\n";
        WriteRaw(msg.c_str(), msg.size());
        posix_fsync(logFd_);
        posix_close(logFd_);
        logFd_ = -1;
        initialized_ = false;
    }
}

void Logger::Log(Level level, const std::string& message) {
    if (level < minLevel_) return;

    std::lock_guard<std::mutex> lock(mutex_);
    std::string entry = GetTimeString() + " [" + GetLevelString(level) + "] " + message + "\n";

    if (initialized_ && logFd_ >= 0) {
        WriteRaw(entry.c_str(), entry.size());
        // fsync on WARNING+ to guarantee persistence; skip for DEBUG/INFO perf
        if (level >= Level::WARNING) {
            posix_fsync(logFd_);
        }
    }

    // Always echo to stderr
    fprintf(stderr, "%s", entry.c_str());
}

void Logger::Debug(const std::string& msg) { Log(Level::DEBUG, msg); }
void Logger::Info(const std::string& msg) { Log(Level::INFO, msg); }
void Logger::Warning(const std::string& msg) { Log(Level::WARNING, msg); }
void Logger::Error(const std::string& msg) { Log(Level::ERR, msg); }
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
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&time));
    char result[40];
    snprintf(result, sizeof(result), "%s.%03d", buf, static_cast<int>(ms.count()));
    return result;
}

std::string Logger::GetLevelString(Level level) {
    switch (level) {
        case Level::DEBUG:    return "DEBUG";
        case Level::INFO:     return "INFO";
        case Level::WARNING:  return "WARNING";
        case Level::ERR:    return "ERROR";
        case Level::CRITICAL: return "CRITICAL";
        default:              return "UNKNOWN";
    }
}

Logger::Level Logger::StringToLevel(const std::string& str) {
    if (str == "DEBUG") return Level::DEBUG;
    if (str == "INFO") return Level::INFO;
    if (str == "WARNING") return Level::WARNING;
    if (str == "ERROR") return Level::ERR;
    if (str == "CRITICAL") return Level::CRITICAL;
    return Level::INFO;
}

std::string Logger::LevelToString(Level level) {
    return GetLevelString(level);
}

} // namespace YipOS
