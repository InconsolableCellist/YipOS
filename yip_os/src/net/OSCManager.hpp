#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <array>
#include <mutex>

#include <cstdint>

#ifdef _WIN32
    using socket_t = uintptr_t; // matches SOCKET (UINT_PTR) on both x86 and x64
    constexpr socket_t INVALID_SOCK = ~socket_t(0); // matches INVALID_SOCKET
#else
    using socket_t = int;
    constexpr socket_t INVALID_SOCK = -1;
#endif

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
    void SendString(const std::string& path, const std::string& value);
    void SendChatbox(const std::string& text, bool send_immediately);

    void SetInputHandler(InputHandler handler);

    // Dynamic port update (for OSCQuery discovery)
    void SetSendTarget(const std::string& address, int port);
    int GetListenPort() const { return listen_port_; }

    bool IsRunning() const { return running_.load(); }

    // Chatbox text received from external apps
    std::string GetChatboxText() const;
    bool HasNewChatboxText();  // returns true once per new message

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

    socket_t send_socket_ = INVALID_SOCK;
    socket_t recv_socket_ = INVALID_SOCK;
    void* server_addr_ = nullptr; // sockaddr_in*, allocated in Initialize
    mutable std::mutex send_mutex_; // protects server_addr_ updates
    int listen_port_ = 0;
    std::thread recv_thread_;
    std::atomic<bool> running_{false};
    InputHandler input_handler_;

    static constexpr size_t MAX_PACKET_SIZE = 8192;
    std::array<char, MAX_PACKET_SIZE> send_buffer_;
    std::array<char, MAX_PACKET_SIZE> recv_buffer_;

    // Chatbox text from external apps
    mutable std::mutex chatbox_mutex_;
    std::string chatbox_text_;
    bool chatbox_new_ = false;

    // Activity log (circular)
    mutable std::mutex log_mutex_;
    std::vector<ParamEntry> recent_sends_;
    std::vector<ParamEntry> recent_recvs_;
    static constexpr size_t MAX_LOG_ENTRIES = 100;
    void LogSend(const std::string& path, float value);
    void LogRecv(const std::string& path, float value);
};

} // namespace YipOS
