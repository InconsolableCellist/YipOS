#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>

namespace YipOS {

struct TwitchMessage {
    std::string from;       // sender display name
    std::string text;       // message text
    int64_t date = 0;       // unix timestamp
};

class TwitchClient {
public:
    TwitchClient();
    ~TwitchClient();

    void SetChannel(const std::string& channel);
    std::string GetChannel() const;

    void Connect();
    void Disconnect();
    bool IsConnected() const { return connected_.load(); }

    // Thread-safe snapshot of messages (newest first)
    std::vector<TwitchMessage> GetMessages() const;
    int GetMessageCount() const;

    // Atomic new-message counter: incremented on each new message,
    // caller reads and compares to detect changes without locking.
    uint64_t GetNewCounter() const { return new_counter_.load(); }

private:
    void RecvLoop();
    void ProcessLine(const std::string& line);
    bool ConnectSocket();
    void CloseSocket();
    void SendRaw(const std::string& msg);

    std::string channel_;
    mutable std::mutex mutex_;

    // Message ring buffer (newest at front)
    std::vector<TwitchMessage> messages_;
    static constexpr int MAX_MESSAGES = 200;

    // Connection state
    std::atomic<bool> connected_{false};
    std::atomic<bool> should_stop_{false};
    std::thread recv_thread_;
    int sock_ = -1;

    // New-message counter
    std::atomic<uint64_t> new_counter_{0};

    // Reconnect backoff
    double reconnect_delay_ = 5.0;
    static constexpr double RECONNECT_MAX = 60.0;
};

} // namespace YipOS
