#include "OSCManager.hpp"
#include "core/Logger.hpp"

#ifdef _WIN32
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

static inline bool SocketValid(socket_t s) { return s != INVALID_SOCK; }

static inline void CloseSocket(socket_t& s) {
    if (!SocketValid(s)) return;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    s = INVALID_SOCK;
}

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
    if (!SocketValid(send_socket_)) {
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
    if (!SocketValid(recv_socket_)) {
        Logger::Error("Failed to create receive socket");
        return false;
    }

    // Allow address reuse (Linux only — on Windows SO_REUSEADDR enables
    // port hijacking and can trigger WSAEACCES)
#ifndef _WIN32
    int opt = 1;
    setsockopt(recv_socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#endif

    sockaddr_in recv_addr{};
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(static_cast<uint16_t>(listen_port));
    recv_addr.sin_addr.s_addr = INADDR_ANY;

#ifdef _WIN32
    if (bind(recv_socket_, reinterpret_cast<sockaddr*>(&recv_addr), sizeof(recv_addr)) == SOCKET_ERROR) {
        Logger::Error("Failed to bind receive socket to port " + std::to_string(listen_port) +
                      " (error " + std::to_string(WSAGetLastError()) + ")");
        return false;
    }
#else
    if (bind(recv_socket_, reinterpret_cast<sockaddr*>(&recv_addr), sizeof(recv_addr)) < 0) {
        Logger::Error("Failed to bind receive socket to port " + std::to_string(listen_port));
        return false;
    }
#endif

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

    listen_port_ = listen_port;
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
    CloseSocket(send_socket_);
    CloseSocket(recv_socket_);
    delete Addr(server_addr_);
    server_addr_ = nullptr;

#ifdef _WIN32
    WSACleanup();
#endif
    Logger::Info("OSC shutdown");
}

void OSCManager::SetSendTarget(const std::string& address, int port) {
    std::lock_guard<std::mutex> lock(send_mutex_);
    if (server_addr_) {
        auto* sa = Addr(server_addr_);
        sa->sin_port = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, address.c_str(), &sa->sin_addr);
        Logger::Info("OSC send target updated: " + address + ":" + std::to_string(port));
    }
}

void OSCManager::SendFloat(const std::string& path, float value) {
    if (!SocketValid(send_socket_) || !server_addr_) return;

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
    if (!SocketValid(send_socket_) || !server_addr_) return;

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
    if (!SocketValid(send_socket_) || !server_addr_) return;

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

void OSCManager::SendString(const std::string& path, const std::string& value) {
    if (!SocketValid(send_socket_) || !server_addr_) return;

    try {
        OSCPP::Client::Packet packet(send_buffer_.data(), send_buffer_.size());
        packet.openMessage(path.c_str(), 1)
              .string(value.c_str())
              .closeMessage();

        sendto(send_socket_,
               static_cast<const char*>(packet.data()),
               static_cast<int>(packet.size()),
               0,
               reinterpret_cast<struct sockaddr*>(server_addr_),
               sizeof(sockaddr_in));

        LogSend(path, 0.0f);
    } catch (const std::exception& e) {
        Logger::Error("OSC SendString error: " + std::string(e.what()));
    }
}

void OSCManager::SendChatbox(const std::string& text, bool send_immediately) {
    if (!SocketValid(send_socket_) || !server_addr_) return;

    // /chatbox/input takes two args: string (s) + bool (T/F)
    // oscpp has no native bool arg, so we build the packet manually.
    try {
        const char* path = "/chatbox/input";
        size_t addr_len = std::strlen(path) + 1;
        size_t addr_padded = (addr_len + 3) & ~3u;

        // Type tag: ",sT\0" or ",sF\0" = 4 bytes
        constexpr size_t tag_padded = 4;

        // String arg: text + null, padded to 4
        size_t str_len = text.size() + 1;
        size_t str_padded = (str_len + 3) & ~3u;

        size_t total = addr_padded + tag_padded + str_padded;
        if (total > send_buffer_.size()) return;

        std::memset(send_buffer_.data(), 0, total);
        std::memcpy(send_buffer_.data(), path, std::strlen(path));

        char* tag = send_buffer_.data() + addr_padded;
        tag[0] = ',';
        tag[1] = 's';
        tag[2] = send_immediately ? 'T' : 'F';

        char* str_data = tag + tag_padded;
        std::memcpy(str_data, text.c_str(), text.size());

        sendto(send_socket_,
               send_buffer_.data(),
               static_cast<int>(total),
               0,
               reinterpret_cast<struct sockaddr*>(server_addr_),
               sizeof(sockaddr_in));

        LogSend("/chatbox/input", 0.0f);
    } catch (const std::exception& e) {
        Logger::Error("OSC SendChatbox error: " + std::string(e.what()));
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

        if (bytes <= 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err != WSAETIMEDOUT && err != WSAECONNRESET) {
                Logger::Debug("recvfrom error: " + std::to_string(err));
            }
#endif
            continue;
        }

        try {
            OSCPP::Server::Packet packet(recv_buffer_.data(), bytes);
            if (packet.isMessage()) {
                OSCPP::Server::Message msg(packet);
                std::string address = msg.address();
                OSCPP::Server::ArgStream args(msg.args());

                // Intercept /chatbox/input string messages
                if (address == "/chatbox/input") {
                    if (!args.atEnd() && args.tag() == 's') {
                        const char* text = args.string();
                        {
                            std::lock_guard<std::mutex> lock(chatbox_mutex_);
                            chatbox_text_ = text ? text : "";
                            chatbox_new_ = true;
                        }
                        Logger::Debug("OSC chatbox: " + chatbox_text_);
                    }
                    continue;
                }

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

                // Only log messages we actually handle
                if (address.find("CRT_Wrist_") != std::string::npos) {
                    LogRecv(address, value);
                }

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

std::string OSCManager::GetChatboxText() const {
    std::lock_guard<std::mutex> lock(chatbox_mutex_);
    return chatbox_text_;
}

bool OSCManager::HasNewChatboxText() {
    std::lock_guard<std::mutex> lock(chatbox_mutex_);
    bool was_new = chatbox_new_;
    chatbox_new_ = false;
    return was_new;
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
