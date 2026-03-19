#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <optional>
#include <unordered_map>
#include <variant>

namespace YipOS {

class OSCQueryServer {
public:
    // OSC type tags
    enum Access { NoAccess = 0, ReadOnly = 1, WriteOnly = 2, ReadWrite = 3 };

    struct ParamNode {
        std::string full_path;
        std::string osc_type; // "f", "i", "T", "F", "s"
        Access access = NoAccess;
        std::variant<float, int, bool, std::string> value;
    };

    OSCQueryServer();
    ~OSCQueryServer();

    bool Start(int osc_udp_port);
    void Stop();

    void AddParameter(const std::string& path, const std::string& osc_type,
                      Access access, std::variant<float, int, bool, std::string> initial_value = 0.0f);
    void UpdateValue(const std::string& path, std::variant<float, int, bool, std::string> value);

    std::optional<int> GetVRChatOSCPort() const;
    std::optional<int> GetVRChatQueryPort() const;
    bool IsVRChatConnected() const;

    int GetHTTPPort() const { return http_port_; }
    int GetOSCPort() const { return osc_port_; }

private:
    void HTTPThread();
    void MDNSBrowseThread();
    void MDNSListenThread();

    std::string BuildHostInfo() const;
    std::string BuildFullTree() const;
    std::string BuildNodeJSON(const std::string& path) const;

    // HTTP server
    std::thread http_thread_;
    std::atomic<bool> running_{false};
    int http_port_ = 0;
    int osc_port_ = 0;
    void* http_server_ = nullptr; // httplib::Server*

    // mDNS
    std::thread mdns_browse_thread_;
    std::thread mdns_listen_thread_;
    int mdns_socket_ = -1;

    // Discovered VRChat endpoints
    mutable std::mutex vrc_mutex_;
    std::optional<int> vrc_osc_port_;
    std::optional<int> vrc_query_port_;

    // Parameter tree
    mutable std::mutex param_mutex_;
    std::vector<ParamNode> params_;
    std::unordered_map<std::string, size_t> param_index_; // path -> index in params_

    std::string service_name_ = "YipOS";
    std::string hostname_;
};

} // namespace YipOS
