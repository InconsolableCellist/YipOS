#include "TwitchClient.hpp"
#include "core/Logger.hpp"
#include <chrono>
#include <cstring>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define CLOSE_SOCKET closesocket
#define SOCKET_ERROR_CODE WSAGetLastError()
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#define CLOSE_SOCKET ::close
#define SOCKET_ERROR_CODE errno
#define INVALID_SOCKET -1
#endif

namespace YipOS {

TwitchClient::TwitchClient() = default;

TwitchClient::~TwitchClient() {
    Disconnect();
}

void TwitchClient::SetChannel(const std::string& channel) {
    std::lock_guard<std::mutex> lock(mutex_);
    channel_ = channel;
}

std::string TwitchClient::GetChannel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return channel_;
}

std::vector<TwitchMessage> TwitchClient::GetMessages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_;
}

int TwitchClient::GetMessageCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(messages_.size());
}

void TwitchClient::Connect() {
    if (recv_thread_.joinable()) return;  // already running

    should_stop_.store(false);
    reconnect_delay_ = 5.0;
    recv_thread_ = std::thread(&TwitchClient::RecvLoop, this);
}

void TwitchClient::Disconnect() {
    should_stop_.store(true);
    CloseSocket();
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
    connected_.store(false);
}

bool TwitchClient::ConnectSocket() {
    std::string channel = GetChannel();
    if (channel.empty()) return false;

    // Resolve host
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo("irc.chat.twitch.tv", "6667", &hints, &result);
    if (err != 0 || !result) {
        Logger::Warning("TwitchClient: DNS resolution failed");
        return false;
    }

    sock_ = static_cast<int>(socket(result->ai_family, result->ai_socktype, result->ai_protocol));
    if (sock_ < 0) {
        freeaddrinfo(result);
        Logger::Warning("TwitchClient: socket() failed");
        return false;
    }

    // Set receive timeout (10s)
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    if (connect(sock_, result->ai_addr, static_cast<socklen_t>(result->ai_addrlen)) != 0) {
        freeaddrinfo(result);
        CloseSocket();
        Logger::Warning("TwitchClient: connect() failed");
        return false;
    }
    freeaddrinfo(result);

    // Anonymous login
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(10000, 99999);
    std::string nick = "justinfan" + std::to_string(dist(rng));

    SendRaw("NICK " + nick + "\r\n");
    SendRaw("JOIN #" + channel + "\r\n");

    Logger::Info("TwitchClient: Connected to #" + channel + " as " + nick);
    connected_.store(true);
    reconnect_delay_ = 5.0;
    return true;
}

void TwitchClient::CloseSocket() {
    if (sock_ >= 0) {
        CLOSE_SOCKET(sock_);
        sock_ = -1;
    }
}

void TwitchClient::SendRaw(const std::string& msg) {
    if (sock_ < 0) return;
    send(sock_, msg.c_str(), static_cast<int>(msg.size()), 0);
}

void TwitchClient::RecvLoop() {
    Logger::Info("TwitchClient: Recv thread started");

    while (!should_stop_.load()) {
        if (!ConnectSocket()) {
            // Backoff before retry
            Logger::Info("TwitchClient: Reconnecting in " +
                         std::to_string(static_cast<int>(reconnect_delay_)) + "s");
            for (int i = 0; i < static_cast<int>(reconnect_delay_ * 10) && !should_stop_.load(); i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            reconnect_delay_ = std::min(reconnect_delay_ * 2.0, RECONNECT_MAX);
            continue;
        }

        // Read loop
        char buf[4096];
        std::string partial;

        while (!should_stop_.load()) {
#ifdef _WIN32
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sock_, &readfds);
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            int sel = select(sock_ + 1, &readfds, nullptr, nullptr, &tv);
            if (sel <= 0) {
                if (sel < 0) break;  // error
                continue;  // timeout, check should_stop
            }
#else
            struct pollfd pfd;
            pfd.fd = sock_;
            pfd.events = POLLIN;
            int sel = poll(&pfd, 1, 1000);  // 1s timeout
            if (sel <= 0) {
                if (sel < 0) break;
                continue;
            }
#endif
            int n = recv(sock_, buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                Logger::Warning("TwitchClient: Connection lost");
                break;
            }
            buf[n] = '\0';
            partial += buf;

            // Split by \r\n
            size_t pos;
            while ((pos = partial.find("\r\n")) != std::string::npos) {
                std::string line = partial.substr(0, pos);
                partial.erase(0, pos + 2);
                if (!line.empty()) {
                    ProcessLine(line);
                }
            }
        }

        CloseSocket();
        connected_.store(false);

        if (!should_stop_.load()) {
            Logger::Info("TwitchClient: Reconnecting in " +
                         std::to_string(static_cast<int>(reconnect_delay_)) + "s");
            for (int i = 0; i < static_cast<int>(reconnect_delay_ * 10) && !should_stop_.load(); i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            reconnect_delay_ = std::min(reconnect_delay_ * 2.0, RECONNECT_MAX);
        }
    }

    Logger::Info("TwitchClient: Recv thread exiting");
}

void TwitchClient::ProcessLine(const std::string& line) {
    // PING :tmi.twitch.tv
    if (line.compare(0, 4, "PING") == 0) {
        SendRaw("PONG" + line.substr(4) + "\r\n");
        return;
    }

    // PRIVMSG format:
    // :username!username@username.tmi.twitch.tv PRIVMSG #channel :message text
    // May have tags prefix: @badges=...;... :username!... PRIVMSG ...
    std::string work = line;

    // Strip IRC tags (start with @)
    if (!work.empty() && work[0] == '@') {
        size_t space = work.find(' ');
        if (space == std::string::npos) return;
        work = work.substr(space + 1);
    }

    // Check for PRIVMSG
    size_t privmsg_pos = work.find(" PRIVMSG ");
    if (privmsg_pos == std::string::npos) return;

    // Extract username from :username!
    if (work.empty() || work[0] != ':') return;
    size_t bang = work.find('!');
    if (bang == std::string::npos || bang > privmsg_pos) return;
    std::string username = work.substr(1, bang - 1);

    // Extract message text (after the second colon following PRIVMSG #channel)
    size_t msg_start = work.find(':', privmsg_pos);
    if (msg_start == std::string::npos) return;
    std::string text = work.substr(msg_start + 1);

    // Create message
    auto now = std::chrono::system_clock::now();
    int64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    TwitchMessage msg;
    msg.from = username;
    msg.text = text;
    msg.date = timestamp;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.insert(messages_.begin(), std::move(msg));
        if (static_cast<int>(messages_.size()) > MAX_MESSAGES) {
            messages_.resize(MAX_MESSAGES);
        }
    }

    new_counter_.fetch_add(1);
    Logger::Debug("TwitchChat: <" + username + "> " + text);
}

} // namespace YipOS
