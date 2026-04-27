#ifdef _WIN32
#define NOMINMAX
#endif
#include "BridgeClient.hpp"
#include "core/Logger.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#else
#include <fcntl.h>
#include <errno.h>
#endif

namespace YipOS {

static double NowSeconds() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

BridgeClient::BridgeClient() {}

BridgeClient::~BridgeClient() {
    Stop();
}

bool BridgeClient::Start(const std::string& host, int port) {
    if (running_) return false;
    host_ = host;
    port_ = port;
    running_ = true;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    thread_ = std::thread(&BridgeClient::ConnectThread, this);
    Logger::Info("BridgeClient: started (target " + host + ":" + std::to_string(port) + ")");
    return true;
}

void BridgeClient::Stop() {
    running_ = false;
    CloseSocket();
    if (thread_.joinable()) thread_.join();
}

CompanionData BridgeClient::GetData() const {
    std::lock_guard<std::mutex> lk(data_mutex_);
    return data_;
}

void BridgeClient::CloseSocket() {
    bridge_socket_t s = socket_;
    if (s != BRIDGE_INVALID_SOCK) {
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        socket_ = BRIDGE_INVALID_SOCK;
    }
    connected_ = false;
}

bool BridgeClient::DoConnect() {
    bridge_socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == BRIDGE_INVALID_SOCK) return false;

    // Store the socket immediately so Stop() can close it mid-connect
    socket_ = s;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        CloseSocket();
        return false;
    }

    if (::connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        socket_ = BRIDGE_INVALID_SOCK;
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        return false;
    }

    if (!running_) {
        CloseSocket();
        return false;
    }

    int flag = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));

    return true;
}

void BridgeClient::SendLine(const std::string& json_str) {
    if (socket_ == BRIDGE_INVALID_SOCK) return;
    std::string data = json_str + "\n";
    const char* ptr = data.c_str();
    size_t remaining = data.size();
    while (remaining > 0) {
        int flags = 0;
#ifdef __linux__
        flags = MSG_NOSIGNAL;
#endif
        auto sent = ::send(socket_, ptr, remaining, flags);
        if (sent <= 0) return;
        ptr += sent;
        remaining -= sent;
    }
}

void BridgeClient::ConnectThread() {
    int backoff_ms = 1000;

    while (running_) {
        if (!DoConnect()) {
            // Backoff: 1s → 2s → 4s → 10s cap
            for (int waited = 0; waited < backoff_ms && running_; waited += 100)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (backoff_ms < 10000) backoff_ms = std::min(backoff_ms * 2, 10000);
            continue;
        }

        backoff_ms = 1000;
        Logger::Info("BridgeClient: connected to " + host_ + ":" + std::to_string(port_));

        // Send hello
        SendLine(R"({"type":"hello","version":1})");
        connected_ = true;

        // Read loop
        std::string line_buf;
        char recv_buf[4096];

        while (running_) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(socket_, &fds);
            struct timeval tv{};
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            int ready = ::select(static_cast<int>(socket_) + 1, &fds, nullptr, nullptr, &tv);
            if (ready < 0) break;
            if (ready == 0) continue;

            auto n = ::recv(socket_, recv_buf, sizeof(recv_buf) - 1, 0);
            if (n <= 0) break;
            recv_buf[n] = '\0';

            line_buf.append(recv_buf, n);
            size_t pos;
            while ((pos = line_buf.find('\n')) != std::string::npos) {
                std::string line = line_buf.substr(0, pos);
                line_buf.erase(0, pos + 1);
                if (!line.empty()) HandleMessage(line);
            }
        }

        Logger::Info("BridgeClient: disconnected");
        CloseSocket();
    }
}

void BridgeClient::RequestPause(const std::vector<std::string>& resources) {
    nlohmann::json j;
    j["type"] = "resource_request";
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : resources)
        arr.push_back({{"name", r}, {"action", "pause"}});
    j["resources"] = arr;
    SendLine(j.dump());
}

void BridgeClient::RequestResume(const std::vector<std::string>& resources) {
    nlohmann::json j;
    j["type"] = "resource_request";
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : resources)
        arr.push_back({{"name", r}, {"action", "resume"}});
    j["resources"] = arr;
    SendLine(j.dump());
}

void BridgeClient::HandleMessage(const std::string& line) {
    try {
        auto j = nlohmann::json::parse(line);
        std::string type = j.value("type", "");

        if (type == "welcome") {
            std::string name = j.value("companion", "");
            std::lock_guard<std::mutex> lk(data_mutex_);
            data_.companion_name = name;
            Logger::Info("BridgeClient: welcome from companion '" + name + "'");
        } else if (type == "display_text") {
            std::lock_guard<std::mutex> lk(data_mutex_);
            data_.display_text = j.value("text", "");
            data_.display_text_time = NowSeconds();
        } else if (type == "expression") {
            std::lock_guard<std::mutex> lk(data_mutex_);
            data_.expression = j.value("name", "");
            data_.expression_time = NowSeconds();
        } else if (type == "think") {
            std::lock_guard<std::mutex> lk(data_mutex_);
            data_.thought = j.value("thought", "");
            data_.thought_time = NowSeconds();
        } else if (type == "thinking") {
            std::lock_guard<std::mutex> lk(data_mutex_);
            data_.thinking = j.value("active", false);
            data_.thinking_time = NowSeconds();
        } else if (type == "ping") {
            SendLine(R"({"type":"pong"})");
        }
    } catch (const std::exception& e) {
        Logger::Warning("BridgeClient: parse error: " + std::string(e.what()));
    }
}

} // namespace YipOS
