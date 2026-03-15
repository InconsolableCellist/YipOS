#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <array>
#include <mutex>

namespace YipOS {

class OSCManager {
public:
    using InputHandler = std::function<void(const std::string& address, float value)>;

    OSCManager();
    ~OSCManager();

    bool Initialize(const std::string& address, int send_port, int listen_port);
    void Shutdown();

    void SendFloat(const std::string& path, float value);
    void SendBool(const std::string& path, bool value);
    void SendInt(const std::string& path, int value);

    void SetInputHandler(InputHandler handler);

    bool IsRunning() const { return running_.load(); }

    // OSC activity tracking for UI
    struct ParamEntry {
        std::string path;
        float value;
        double timestamp;
    };
    std::vector<ParamEntry> GetRecentSends() const;
    std::vector<ParamEntry> GetRecentRecvs() const;

private:
    void ReceiveThread();

    int send_socket_ = -1;
    int recv_socket_ = -1;
    void* server_addr_ = nullptr; // sockaddr_in*, allocated in Initialize
    std::thread recv_thread_;
    std::atomic<bool> running_{false};
    InputHandler input_handler_;

    static constexpr size_t MAX_PACKET_SIZE = 8192;
    std::array<char, MAX_PACKET_SIZE> send_buffer_;
    std::array<char, MAX_PACKET_SIZE> recv_buffer_;

    // Activity log (circular)
    mutable std::mutex log_mutex_;
    std::vector<ParamEntry> recent_sends_;
    std::vector<ParamEntry> recent_recvs_;
    static constexpr size_t MAX_LOG_ENTRIES = 100;
    void LogSend(const std::string& path, float value);
    void LogRecv(const std::string& path, float value);
};

} // namespace YipOS
