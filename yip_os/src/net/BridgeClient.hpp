#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>

#ifdef _WIN32
#include <WS2tcpip.h>
    using bridge_socket_t = uintptr_t;
    constexpr bridge_socket_t BRIDGE_INVALID_SOCK = ~bridge_socket_t(0);
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
    using bridge_socket_t = int;
    constexpr bridge_socket_t BRIDGE_INVALID_SOCK = -1;
#endif

namespace YipOS {

struct CompanionData {
    std::string display_text;
    std::string expression;
    std::string thought;
    std::string companion_name;
    bool thinking = false;
    double display_text_time = 0;
    double expression_time = 0;
    double thought_time = 0;
    double thinking_time = 0;
};

class BridgeClient {
public:
    BridgeClient();
    ~BridgeClient();

    bool Start(const std::string& host, int port);
    void Stop();
    bool IsConnected() const { return connected_; }

    CompanionData GetData() const;
    void RequestPause(const std::vector<std::string>& resources);
    void RequestResume(const std::vector<std::string>& resources);

private:
    void ConnectThread();
    void HandleMessage(const std::string& line);
    void CloseSocket();
    bool DoConnect();
    void SendLine(const std::string& json_str);

    std::string host_;
    int port_ = 9200;
    bridge_socket_t socket_ = BRIDGE_INVALID_SOCK;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread thread_;

    mutable std::mutex data_mutex_;
    CompanionData data_;
};

} // namespace YipOS
