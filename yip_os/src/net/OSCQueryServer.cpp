#include "OSCQueryServer.hpp"
#include "core/Logger.hpp"

#ifdef _WIN32
    #include <WS2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <ifaddrs.h>
    #include <net/if.h>
#endif

#define CPPHTTPLIB_NO_EXCEPTIONS
#include <cpp-httplib/httplib.h>

// mdns.h is a C library; include in extern "C" isn't needed (it has its own guard)
#include <mdns/mdns.h>

#include <nlohmann/json.hpp>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace YipOS {

using json = nlohmann::json;

// Helper: get local hostname
static std::string GetLocalHostname() {
    char buf[256] = {};
#ifdef _WIN32
    DWORD size = sizeof(buf);
    GetComputerNameA(buf, &size);
#else
    gethostname(buf, sizeof(buf));
#endif
    std::string name = buf;
    if (name.empty()) name = "yipos-host";
    return name;
}

// Helper: get local IPv4 address (non-loopback)
static std::string GetLocalIPv4() {
#ifdef _WIN32
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) return "127.0.0.1";
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(hostname, nullptr, &hints, &result) != 0) return "127.0.0.1";
    std::string ip = "127.0.0.1";
    for (auto* p = result; p; p = p->ai_next) {
        char buf[INET_ADDRSTRLEN];
        auto* addr = reinterpret_cast<sockaddr_in*>(p->ai_addr);
        inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf));
        std::string s = buf;
        if (s != "127.0.0.1") { ip = s; break; }
    }
    freeaddrinfo(result);
    return ip;
#else
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) return "127.0.0.1";
    std::string ip = "127.0.0.1";
    for (auto* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        char buf[INET_ADDRSTRLEN];
        auto* addr = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
        inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf));
        ip = buf;
        break;
    }
    freeifaddrs(ifaddr);
    return ip;
#endif
}

// Helper: find an available TCP port
static int FindAvailableTCPPort() {
    int sock = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (sock < 0) return 0;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0; // OS picks
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return 0;
    }
    socklen_t len = sizeof(addr);
    getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len);
    int port = ntohs(addr.sin_port);
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return port;
}

// JSON value helper
static json ValueToJSON(const std::variant<float, int, bool, std::string>& v) {
    if (auto* f = std::get_if<float>(&v)) return json::array({*f});
    if (auto* i = std::get_if<int>(&v)) return json::array({*i});
    if (auto* b = std::get_if<bool>(&v)) return json::array({*b});
    if (auto* s = std::get_if<std::string>(&v)) return json::array({*s});
    return json::array({0});
}

OSCQueryServer::OSCQueryServer() {
    hostname_ = GetLocalHostname();
}

OSCQueryServer::~OSCQueryServer() {
    Stop();
}

bool OSCQueryServer::Start(int osc_udp_port) {
    if (running_) return true;

    osc_port_ = osc_udp_port;
    http_port_ = FindAvailableTCPPort();
    if (http_port_ == 0) {
        Logger::Error("OSCQuery: failed to find available TCP port");
        return false;
    }

    running_ = true;

    // Start HTTP server thread
    http_thread_ = std::thread(&OSCQueryServer::HTTPThread, this);

    // Start mDNS threads
    mdns_browse_thread_ = std::thread(&OSCQueryServer::MDNSBrowseThread, this);
    mdns_listen_thread_ = std::thread(&OSCQueryServer::MDNSListenThread, this);

    Logger::Info("OSCQuery started: HTTP port=" + std::to_string(http_port_) +
                 " OSC port=" + std::to_string(osc_port_));
    return true;
}

void OSCQueryServer::Stop() {
    if (!running_) return;
    running_ = false;

    // Stop HTTP server
    if (http_server_) {
        static_cast<httplib::Server*>(http_server_)->stop();
    }

    // Close mDNS socket to unblock recvfrom
    if (mdns_socket_ >= 0) {
        mdns_socket_close(mdns_socket_);
        mdns_socket_ = -1;
    }

    if (http_thread_.joinable()) http_thread_.join();
    if (mdns_browse_thread_.joinable()) mdns_browse_thread_.join();
    if (mdns_listen_thread_.joinable()) mdns_listen_thread_.join();

    http_server_ = nullptr;
    Logger::Info("OSCQuery stopped");
}

void OSCQueryServer::AddParameter(const std::string& path, const std::string& osc_type,
                                   Access access, std::variant<float, int, bool, std::string> initial_value) {
    std::lock_guard<std::mutex> lock(param_mutex_);
    auto it = param_index_.find(path);
    if (it != param_index_.end()) {
        // Update existing
        params_[it->second].osc_type = osc_type;
        params_[it->second].access = access;
        params_[it->second].value = initial_value;
        return;
    }
    param_index_[path] = params_.size();
    params_.push_back({path, osc_type, access, initial_value});
}

void OSCQueryServer::UpdateValue(const std::string& path, std::variant<float, int, bool, std::string> value) {
    std::lock_guard<std::mutex> lock(param_mutex_);
    auto it = param_index_.find(path);
    if (it != param_index_.end()) {
        params_[it->second].value = value;
    }
}

std::optional<int> OSCQueryServer::GetVRChatOSCPort() const {
    std::lock_guard<std::mutex> lock(vrc_mutex_);
    return vrc_osc_port_;
}

std::optional<int> OSCQueryServer::GetVRChatQueryPort() const {
    std::lock_guard<std::mutex> lock(vrc_mutex_);
    return vrc_query_port_;
}

bool OSCQueryServer::IsVRChatConnected() const {
    std::lock_guard<std::mutex> lock(vrc_mutex_);
    return vrc_osc_port_.has_value();
}

// --- HTTP Server ---

std::string OSCQueryServer::BuildHostInfo() const {
    json j;
    j["NAME"] = service_name_;
    j["OSC_PORT"] = osc_port_;
    j["OSC_TRANSPORT"] = "UDP";
    j["EXTENSIONS"]["ACCESS"] = true;
    j["EXTENSIONS"]["VALUE"] = true;
    return j.dump();
}

// Build a JSON node for a single parameter
static json MakeParamJSON(const OSCQueryServer::ParamNode& p) {
    json node;
    node["FULL_PATH"] = p.full_path;
    node["TYPE"] = p.osc_type;
    node["ACCESS"] = static_cast<int>(p.access);
    node["VALUE"] = ValueToJSON(p.value);
    return node;
}

// Build a container node (directory in the OSC address tree)
static json MakeContainerJSON(const std::string& path) {
    json node;
    node["FULL_PATH"] = path;
    node["CONTENTS"] = json::object();
    return node;
}

std::string OSCQueryServer::BuildFullTree() const {
    std::lock_guard<std::mutex> lock(param_mutex_);

    // Root node
    json root = MakeContainerJSON("/");

    for (auto& p : params_) {
        // Split path into segments, e.g. "/avatar/parameters/WT_CursorX"
        // -> ["avatar", "parameters", "WT_CursorX"]
        std::vector<std::string> segments;
        std::istringstream ss(p.full_path.substr(1)); // skip leading /
        std::string seg;
        while (std::getline(ss, seg, '/')) {
            if (!seg.empty()) segments.push_back(seg);
        }

        // Walk/create the tree
        json* current = &root;
        std::string built_path;
        for (size_t i = 0; i < segments.size(); i++) {
            built_path += "/" + segments[i];
            bool is_leaf = (i == segments.size() - 1);

            if (!current->contains("CONTENTS")) {
                (*current)["CONTENTS"] = json::object();
            }

            auto& contents = (*current)["CONTENTS"];
            if (!contents.contains(segments[i])) {
                if (is_leaf) {
                    contents[segments[i]] = MakeParamJSON(p);
                } else {
                    contents[segments[i]] = MakeContainerJSON(built_path);
                }
            }
            current = &contents[segments[i]];
        }
    }

    return root.dump();
}

std::string OSCQueryServer::BuildNodeJSON(const std::string& path) const {
    std::lock_guard<std::mutex> lock(param_mutex_);
    auto it = param_index_.find(path);
    if (it != param_index_.end()) {
        return MakeParamJSON(params_[it->second]).dump();
    }

    // Check if it's a container path (prefix of any parameter)
    json container = MakeContainerJSON(path);
    bool found = false;
    for (auto& p : params_) {
        if (p.full_path.rfind(path, 0) == 0 && p.full_path.size() > path.size()) {
            // p is under this path
            std::string remainder = p.full_path.substr(path.size());
            if (remainder[0] == '/') remainder = remainder.substr(1);
            auto slash = remainder.find('/');
            std::string child = (slash != std::string::npos) ? remainder.substr(0, slash) : remainder;

            if (!container["CONTENTS"].contains(child)) {
                if (slash == std::string::npos) {
                    container["CONTENTS"][child] = MakeParamJSON(p);
                } else {
                    std::string child_path = path + "/" + child;
                    container["CONTENTS"][child] = MakeContainerJSON(child_path);
                }
                found = true;
            }
        }
    }

    if (found) return container.dump();
    return ""; // not found
}

void OSCQueryServer::HTTPThread() {
    auto server = new httplib::Server();
    http_server_ = server;

    // GET / — full tree or HOST_INFO
    server->Get("/", [this](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("HOST_INFO")) {
            res.set_content(BuildHostInfo(), "application/json");
        } else {
            res.set_content(BuildFullTree(), "application/json");
        }
    });

    // GET /any/path — individual node lookup
    server->Get(".*", [this](const httplib::Request& req, httplib::Response& res) {
        std::string path = req.path;
        if (path == "/") return; // handled above

        if (req.has_param("HOST_INFO")) {
            res.set_content(BuildHostInfo(), "application/json");
            return;
        }

        std::string body = BuildNodeJSON(path);
        if (body.empty()) {
            res.status = 404;
            res.set_content("{}", "application/json");
        } else {
            res.set_content(body, "application/json");
        }
    });

    Logger::Info("OSCQuery HTTP server listening on port " + std::to_string(http_port_));
    server->listen("0.0.0.0", http_port_);

    // Server stopped
    delete server;
    http_server_ = nullptr;
}

// --- mDNS Browse (discover VRChat) ---

struct BrowseContext {
    OSCQueryServer* self;
    std::optional<int> osc_port;
    std::optional<int> query_port;
};

static int MDNSBrowseCallback(int sock, const struct sockaddr* from, size_t addrlen,
                               mdns_entry_type_t entry, uint16_t query_id, uint16_t rtype,
                               uint16_t rclass, uint32_t ttl, const void* data, size_t size,
                               size_t name_offset, size_t name_length, size_t record_offset,
                               size_t record_length, void* user_data) {
    auto* ctx = static_cast<BrowseContext*>(user_data);
    if (rtype != MDNS_RECORDTYPE_SRV) return 0;

    char name_buf[256] = {};
    mdns_string_t name = mdns_string_extract(data, size, &name_offset, name_buf, sizeof(name_buf));
    std::string service_name(name.str, name.length);

    // Parse SRV record for port
    mdns_record_srv_t srv = mdns_record_parse_srv(data, size, record_offset, record_length,
                                                   name_buf, sizeof(name_buf));

    if (service_name.find("_osc._udp") != std::string::npos &&
        service_name.find("VRChat") != std::string::npos) {
        ctx->osc_port = srv.port;
        Logger::Info("OSCQuery: found VRChat OSC at port " + std::to_string(srv.port));
    }
    else if (service_name.find("_oscjson._tcp") != std::string::npos &&
             service_name.find("VRChat") != std::string::npos) {
        ctx->query_port = srv.port;
        Logger::Info("OSCQuery: found VRChat OSCQuery at port " + std::to_string(srv.port));
    }

    return 0;
}

void OSCQueryServer::MDNSBrowseThread() {
    Logger::Info("OSCQuery mDNS browse thread started");

    while (running_) {
        // Open a socket for sending queries
        int sock = mdns_socket_open_ipv4(nullptr);
        if (sock < 0) {
            Logger::Warning("OSCQuery: failed to open mDNS query socket");
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        BrowseContext ctx{this, std::nullopt, std::nullopt};
        char buffer[2048];

        // Send browse queries for both service types
        mdns_query_t queries[2] = {
            {MDNS_RECORDTYPE_PTR, MDNS_STRING_CONST("_oscjson._tcp.local.")},
            {MDNS_RECORDTYPE_PTR, MDNS_STRING_CONST("_osc._udp.local.")}
        };

        mdns_multiquery_send(sock, queries, 2, buffer, sizeof(buffer), 0);

        // Receive responses for up to 2 seconds
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline && running_) {
            // Set a short recv timeout
#ifdef _WIN32
            DWORD tv = 200;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 200000;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
            mdns_query_recv(sock, buffer, sizeof(buffer), MDNSBrowseCallback, &ctx, 0);
        }

        mdns_socket_close(sock);

        // Update discovered ports
        if (ctx.osc_port || ctx.query_port) {
            std::lock_guard<std::mutex> lock(vrc_mutex_);
            if (ctx.osc_port) vrc_osc_port_ = ctx.osc_port;
            if (ctx.query_port) vrc_query_port_ = ctx.query_port;
        }

        // Browse every 5 seconds
        for (int i = 0; i < 50 && running_; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    Logger::Info("OSCQuery mDNS browse thread stopped");
}

// --- mDNS Listen (announce our services, respond to queries) ---

struct ListenContext {
    OSCQueryServer* self;
    int sock;
    std::string service_name;
    std::string hostname;
    int http_port;
    int osc_port;
    struct sockaddr_in local_addr;
};

static int MDNSListenCallback(int sock, const struct sockaddr* from, size_t addrlen,
                               mdns_entry_type_t entry, uint16_t query_id, uint16_t rtype,
                               uint16_t rclass, uint32_t ttl, const void* data, size_t size,
                               size_t name_offset, size_t name_length, size_t record_offset,
                               size_t record_length, void* user_data) {
    auto* ctx = static_cast<ListenContext*>(user_data);

    if (entry != MDNS_ENTRYTYPE_QUESTION) return 0;

    char name_buf[256] = {};
    mdns_string_t name = mdns_string_extract(data, size, &name_offset, name_buf, sizeof(name_buf));
    std::string query_name(name.str, name.length);

    char sendbuf[2048];

    // Service name strings for mDNS records
    std::string oscjson_service = ctx->service_name + "._oscjson._tcp.local.";
    std::string osc_service = ctx->service_name + "._osc._udp.local.";
    std::string host = ctx->hostname + ".local.";

    // Answer _oscjson._tcp queries
    if (query_name.find("_oscjson._tcp.local") != std::string::npos) {
        // Build answer: PTR -> SRV -> A
        mdns_record_t answer = {};
        answer.name = {MDNS_STRING_CONST("_oscjson._tcp.local.")};
        answer.type = MDNS_RECORDTYPE_PTR;
        answer.data.ptr.name = {oscjson_service.c_str(), oscjson_service.size()};
        answer.rclass = 0;
        answer.ttl = 120;

        mdns_record_t additional[2] = {};
        // SRV record
        additional[0].name = {oscjson_service.c_str(), oscjson_service.size()};
        additional[0].type = MDNS_RECORDTYPE_SRV;
        additional[0].data.srv.name = {host.c_str(), host.size()};
        additional[0].data.srv.port = static_cast<uint16_t>(ctx->http_port);
        additional[0].data.srv.priority = 0;
        additional[0].data.srv.weight = 0;
        additional[0].rclass = 0;
        additional[0].ttl = 120;

        // A record
        additional[1].name = {host.c_str(), host.size()};
        additional[1].type = MDNS_RECORDTYPE_A;
        additional[1].data.a.addr = ctx->local_addr;
        additional[1].rclass = 0;
        additional[1].ttl = 120;

        mdns_query_answer_multicast(sock, sendbuf, sizeof(sendbuf),
                                     answer, nullptr, 0, additional, 2);
    }

    // Answer _osc._udp queries
    if (query_name.find("_osc._udp.local") != std::string::npos) {
        mdns_record_t answer = {};
        answer.name = {MDNS_STRING_CONST("_osc._udp.local.")};
        answer.type = MDNS_RECORDTYPE_PTR;
        answer.data.ptr.name = {osc_service.c_str(), osc_service.size()};
        answer.rclass = 0;
        answer.ttl = 120;

        mdns_record_t additional[2] = {};
        additional[0].name = {osc_service.c_str(), osc_service.size()};
        additional[0].type = MDNS_RECORDTYPE_SRV;
        additional[0].data.srv.name = {host.c_str(), host.size()};
        additional[0].data.srv.port = static_cast<uint16_t>(ctx->osc_port);
        additional[0].data.srv.priority = 0;
        additional[0].data.srv.weight = 0;
        additional[0].rclass = 0;
        additional[0].ttl = 120;

        additional[1].name = {host.c_str(), host.size()};
        additional[1].type = MDNS_RECORDTYPE_A;
        additional[1].data.a.addr = ctx->local_addr;
        additional[1].rclass = 0;
        additional[1].ttl = 120;

        mdns_query_answer_multicast(sock, sendbuf, sizeof(sendbuf),
                                     answer, nullptr, 0, additional, 2);
    }

    return 0;
}

void OSCQueryServer::MDNSListenThread() {
    Logger::Info("OSCQuery mDNS listen thread started");

    // Open socket bound to MDNS_PORT for listening
    struct sockaddr_in saddr{};
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(MDNS_PORT);
    saddr.sin_addr.s_addr = INADDR_ANY;

    int sock = mdns_socket_open_ipv4(&saddr);
    if (sock < 0) {
        Logger::Warning("OSCQuery: failed to open mDNS listen socket (port 5353 may be in use)");
        return;
    }

    mdns_socket_ = sock;

    // Resolve local address for A records
    std::string local_ip = GetLocalIPv4();
    struct sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    inet_pton(AF_INET, local_ip.c_str(), &local_addr.sin_addr);

    ListenContext ctx{};
    ctx.self = this;
    ctx.sock = sock;
    ctx.service_name = service_name_;
    ctx.hostname = hostname_;
    ctx.http_port = http_port_;
    ctx.osc_port = osc_port_;
    ctx.local_addr = local_addr;

    // Send initial announcement
    {
        char sendbuf[2048];
        std::string oscjson_service = service_name_ + "._oscjson._tcp.local.";
        std::string osc_service = service_name_ + "._osc._udp.local.";
        std::string host = hostname_ + ".local.";

        // Announce _oscjson._tcp
        {
            mdns_record_t answer = {};
            answer.name = {oscjson_service.c_str(), oscjson_service.size()};
            answer.type = MDNS_RECORDTYPE_SRV;
            answer.data.srv.name = {host.c_str(), host.size()};
            answer.data.srv.port = static_cast<uint16_t>(http_port_);
            answer.data.srv.priority = 0;
            answer.data.srv.weight = 0;
            answer.rclass = 0;
            answer.ttl = 120;

            mdns_record_t additional = {};
            additional.name = {host.c_str(), host.size()};
            additional.type = MDNS_RECORDTYPE_A;
            additional.data.a.addr = local_addr;
            additional.rclass = 0;
            additional.ttl = 120;

            mdns_announce_multicast(sock, sendbuf, sizeof(sendbuf),
                                    answer, nullptr, 0, &additional, 1);
        }

        // Announce _osc._udp
        {
            mdns_record_t answer = {};
            answer.name = {osc_service.c_str(), osc_service.size()};
            answer.type = MDNS_RECORDTYPE_SRV;
            answer.data.srv.name = {host.c_str(), host.size()};
            answer.data.srv.port = static_cast<uint16_t>(osc_port_);
            answer.data.srv.priority = 0;
            answer.data.srv.weight = 0;
            answer.rclass = 0;
            answer.ttl = 120;

            mdns_record_t additional = {};
            additional.name = {host.c_str(), host.size()};
            additional.type = MDNS_RECORDTYPE_A;
            additional.data.a.addr = local_addr;
            additional.rclass = 0;
            additional.ttl = 120;

            mdns_announce_multicast(sock, sendbuf, sizeof(sendbuf),
                                    answer, nullptr, 0, &additional, 1);
        }

        Logger::Info("OSCQuery: announced services (oscjson tcp:" + std::to_string(http_port_) +
                     " osc udp:" + std::to_string(osc_port_) + ")");
    }

    char buffer[2048];
    while (running_) {
        mdns_socket_listen(sock, buffer, sizeof(buffer), MDNSListenCallback, &ctx);
    }

    // Send goodbye
    {
        char sendbuf[2048];
        std::string oscjson_service = service_name_ + "._oscjson._tcp.local.";
        std::string host = hostname_ + ".local.";

        mdns_record_t answer = {};
        answer.name = {oscjson_service.c_str(), oscjson_service.size()};
        answer.type = MDNS_RECORDTYPE_SRV;
        answer.data.srv.name = {host.c_str(), host.size()};
        answer.data.srv.port = static_cast<uint16_t>(http_port_);
        answer.rclass = 0;
        answer.ttl = 0;

        mdns_goodbye_multicast(sock, sendbuf, sizeof(sendbuf), answer, nullptr, 0, nullptr, 0);
    }

    if (mdns_socket_ >= 0) {
        mdns_socket_close(mdns_socket_);
        mdns_socket_ = -1;
    }

    Logger::Info("OSCQuery mDNS listen thread stopped");
}

} // namespace YipOS
