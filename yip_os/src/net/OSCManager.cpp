#include "OSCManager.hpp"
#include "core/Logger.hpp"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <WinSock2.h>
    #include <WS2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socklen_t = int;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

#include <cstring>

#include <oscpp/client.hpp>
#include <oscpp/server.hpp>
#include <chrono>

namespace YipOS {

static double Now() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Helper to cast void* server_addr_ to sockaddr_in*
static sockaddr_in* Addr(void* p) { return static_cast<sockaddr_in*>(p); }

OSCManager::OSCManager() = default;

OSCManager::~OSCManager() {
    Shutdown();
}

bool OSCManager::Initialize(const std::string& address, int send_port, int listen_port) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        Logger::Error("WSAStartup failed");
        return false;
    }
#endif

    // Send socket
    send_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (send_socket_ < 0) {
        Logger::Error("Failed to create send socket");
        return false;
    }

    auto* sa = new sockaddr_in{};
    sa->sin_family = AF_INET;
    sa->sin_port = htons(static_cast<uint16_t>(send_port));
    inet_pton(AF_INET, address.c_str(), &sa->sin_addr);
    server_addr_ = sa;

    // Receive socket
    recv_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (recv_socket_ < 0) {
        Logger::Error("Failed to create receive socket");
        return false;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(recv_socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in recv_addr{};
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(static_cast<uint16_t>(listen_port));
    recv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(recv_socket_, reinterpret_cast<sockaddr*>(&recv_addr), sizeof(recv_addr)) < 0) {
        Logger::Error("Failed to bind receive socket to port " + std::to_string(listen_port));
        return false;
    }

    // Set receive timeout (500ms) for graceful shutdown
#ifdef _WIN32
    DWORD timeout_ms = 500;
    setsockopt(recv_socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    setsockopt(recv_socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif

    running_ = true;
    recv_thread_ = std::thread(&OSCManager::ReceiveThread, this);

    Logger::Info("OSC initialized: send=" + address + ":" + std::to_string(send_port) +
                 " listen=0.0.0.0:" + std::to_string(listen_port));
    return true;
}

void OSCManager::Shutdown() {
    running_ = false;
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
    if (send_socket_ >= 0) {
#ifdef _WIN32
        closesocket(send_socket_);
#else
        close(send_socket_);
#endif
        send_socket_ = -1;
    }
    if (recv_socket_ >= 0) {
#ifdef _WIN32
        closesocket(recv_socket_);
#else
        close(recv_socket_);
#endif
        recv_socket_ = -1;
    }
    delete Addr(server_addr_);
    server_addr_ = nullptr;

#ifdef _WIN32
    WSACleanup();
#endif
    Logger::Info("OSC shutdown");
}

void OSCManager::SendFloat(const std::string& path, float value) {
    if (send_socket_ < 0 || !server_addr_) return;

    try {
        OSCPP::Client::Packet packet(send_buffer_.data(), send_buffer_.size());
        packet.openMessage(path.c_str(), 1)
              .float32(value)
              .closeMessage();

        sendto(send_socket_,
               static_cast<const char*>(packet.data()),
               static_cast<int>(packet.size()),
               0,
               reinterpret_cast<struct sockaddr*>(server_addr_),
               sizeof(sockaddr_in));

        LogSend(path, value);
    } catch (const std::exception& e) {
        Logger::Error("OSC SendFloat error: " + std::string(e.what()));
    }
}

void OSCManager::SendBool(const std::string& path, bool value) {
    if (send_socket_ < 0 || !server_addr_) return;

    // OSC bools are type tags 'T'/'F' with zero data bytes.
    // oscpp has no native bool method, so we build the packet manually.
    // Layout: address (padded to 4), type tag string ",T\0\0" or ",F\0\0" (4 bytes)
    try {
        // Compute padded address length
        size_t addr_len = path.size() + 1; // include null
        size_t addr_padded = (addr_len + 3) & ~3u; // align to 4

        // Type tag string: ",T\0" or ",F\0" padded to 4 bytes = ",T\0\0" / ",F\0\0"
        constexpr size_t tag_padded = 4;

        size_t total = addr_padded + tag_padded;
        if (total > send_buffer_.size()) return;

        std::memset(send_buffer_.data(), 0, total);
        std::memcpy(send_buffer_.data(), path.c_str(), path.size());
        char* tag = send_buffer_.data() + addr_padded;
        tag[0] = ',';
        tag[1] = value ? 'T' : 'F';

        sendto(send_socket_,
               send_buffer_.data(),
               static_cast<int>(total),
               0,
               reinterpret_cast<struct sockaddr*>(server_addr_),
               sizeof(sockaddr_in));

        LogSend(path, value ? 1.0f : 0.0f);
    } catch (const std::exception& e) {
        Logger::Error("OSC SendBool error: " + std::string(e.what()));
    }
}

void OSCManager::SendInt(const std::string& path, int value) {
    if (send_socket_ < 0 || !server_addr_) return;

    try {
        OSCPP::Client::Packet packet(send_buffer_.data(), send_buffer_.size());
        packet.openMessage(path.c_str(), 1)
              .int32(value)
              .closeMessage();

        sendto(send_socket_,
               static_cast<const char*>(packet.data()),
               static_cast<int>(packet.size()),
               0,
               reinterpret_cast<struct sockaddr*>(server_addr_),
               sizeof(sockaddr_in));

        LogSend(path, static_cast<float>(value));
    } catch (const std::exception& e) {
        Logger::Error("OSC SendInt error: " + std::string(e.what()));
    }
}

void OSCManager::SetInputHandler(InputHandler handler) {
    input_handler_ = std::move(handler);
}

void OSCManager::ReceiveThread() {
    Logger::Info("OSC receive thread started");
    while (running_) {
        sockaddr_in sender_addr{};
        socklen_t sender_len = sizeof(sender_addr);

        int bytes = recvfrom(recv_socket_, recv_buffer_.data(),
                            static_cast<int>(recv_buffer_.size()), 0,
                            reinterpret_cast<sockaddr*>(&sender_addr), &sender_len);

        if (bytes <= 0) continue;

        try {
            OSCPP::Server::Packet packet(recv_buffer_.data(), bytes);
            if (packet.isMessage()) {
                OSCPP::Server::Message msg(packet);
                std::string address = msg.address();
                OSCPP::Server::ArgStream args(msg.args());

                float value = 0.0f;
                if (!args.atEnd()) {
                    char tag = args.tag();
                    if (tag == 'f') {
                        value = args.float32();
                    } else if (tag == 'i') {
                        value = static_cast<float>(args.int32());
                    } else if (tag == 'T') {
                        value = 1.0f;
                        args.drop();
                    } else if (tag == 'F') {
                        value = 0.0f;
                        args.drop();
                    }
                }

                LogRecv(address, value);

                if (input_handler_) {
                    input_handler_(address, value);
                }
            }
        } catch (const std::exception& e) {
            Logger::Debug("OSC parse error: " + std::string(e.what()));
        }
    }
    Logger::Info("OSC receive thread stopped");
}

void OSCManager::LogSend(const std::string& path, float value) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (recent_sends_.size() >= MAX_LOG_ENTRIES) {
        recent_sends_.erase(recent_sends_.begin());
    }
    recent_sends_.push_back({path, value, Now()});
}

void OSCManager::LogRecv(const std::string& path, float value) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (recent_recvs_.size() >= MAX_LOG_ENTRIES) {
        recent_recvs_.erase(recent_recvs_.begin());
    }
    recent_recvs_.push_back({path, value, Now()});
}

std::vector<OSCManager::ParamEntry> OSCManager::GetRecentSends() const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    return recent_sends_;
}

std::vector<OSCManager::ParamEntry> OSCManager::GetRecentRecvs() const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    return recent_recvs_;
}

} // namespace YipOS
