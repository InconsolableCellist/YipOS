#include "DMClient.hpp"
#include "core/Logger.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <cstring>

// Windows <windows.h> (pulled in by curl) #defines SendMessage → SendMessageA/W
#ifdef SendMessage
#undef SendMessage
#endif

namespace YipOS {

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

DMClient::DMClient() {
    curl_ = curl_easy_init();
}

DMClient::~DMClient() {
    if (curl_) curl_easy_cleanup(curl_);
}

void DMClient::SetEndpoint(const std::string& url) {
    endpoint_ = url;
    // Strip trailing slash
    if (!endpoint_.empty() && endpoint_.back() == '/')
        endpoint_.pop_back();
}

// --- HTTP helpers ---

std::string DMClient::PostJSON(const std::string& path, const std::string& body) {
    if (!curl_ || endpoint_.empty()) return "";

    std::string url = endpoint_ + path;
    std::string response;

    curl_easy_reset(curl_);
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 5L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl_);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        Logger::Warning("DMClient POST " + path + " failed: " + curl_easy_strerror(res));
        return "";
    }

    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code >= 300) {
        std::string detail = response;
        if (detail.size() > 200) detail = detail.substr(0, 200) + "...";
        Logger::Warning("DMClient POST " + path + " HTTP " + std::to_string(http_code) +
                        ": " + detail);
        return "";
    }

    return response;
}

std::string DMClient::Get(const std::string& url) {
    if (!curl_ || endpoint_.empty()) return "";

    std::string full_url = endpoint_ + url;
    std::string response;

    curl_easy_reset(curl_);
    curl_easy_setopt(curl_, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        Logger::Warning("DMClient GET " + url + " failed: " + curl_easy_strerror(res));
        return "";
    }

    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        std::string detail = response;
        if (detail.size() > 200) detail = detail.substr(0, 200) + "...";
        Logger::Warning("DMClient GET " + url + " HTTP " + std::to_string(http_code) +
                        ": " + detail);
        return "";
    }

    return response;
}

// --- Minimal JSON helpers ---
// Parse a flat JSON object into key-value string pairs.
// Handles: {"key":"value", "key2": 123, "key3": true}
bool DMClient::ParseSimpleJSON(const std::string& json,
                                std::vector<std::pair<std::string, std::string>>& out) {
    out.clear();
    size_t pos = 0;
    while (pos < json.size() && json[pos] != '{') pos++;
    if (pos >= json.size()) return false;
    pos++;

    while (pos < json.size() && json[pos] != '}') {
        // Skip whitespace/commas
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' ||
               json[pos] == '\r' || json[pos] == '\t' || json[pos] == ','))
            pos++;
        if (pos >= json.size() || json[pos] == '}') break;

        // Key
        if (json[pos] != '"') return false;
        pos++;
        size_t ks = pos;
        while (pos < json.size() && json[pos] != '"') pos++;
        if (pos >= json.size()) return false;
        std::string key = json.substr(ks, pos - ks);
        pos++;

        // Colon
        while (pos < json.size() && json[pos] != ':') pos++;
        if (pos >= json.size()) return false;
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

        // Value
        std::string value;
        if (json[pos] == '"') {
            pos++;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\' && pos + 1 < json.size()) {
                    pos++;
                    switch (json[pos]) {
                        case '"': value += '"'; break;
                        case '\\': value += '\\'; break;
                        case 'n': value += '\n'; break;
                        case '/': value += '/'; break;
                        default: value += json[pos]; break;
                    }
                } else {
                    value += json[pos];
                }
                pos++;
            }
            if (pos < json.size()) pos++;
        } else {
            size_t vs = pos;
            while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ' ')
                pos++;
            value = json.substr(vs, pos - vs);
        }

        out.push_back({key, value});
    }
    return true;
}

// Escape a string for JSON output
static std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

static std::string FindVal(const std::vector<std::pair<std::string, std::string>>& kv,
                           const std::string& key) {
    for (auto& p : kv) {
        if (p.first == key) return p.second;
    }
    return "";
}

// --- Pairing ---

bool DMClient::PairCreate(std::string& out_code, std::string& out_session_id) {
    std::string body = "{\"user_id\":\"" + JsonEscape(user_id_) +
                       "\",\"display_name\":\"" + JsonEscape(display_name_) + "\"}";

    std::string resp = PostJSON("/dm/pair/create", body);
    if (resp.empty()) return false;

    std::vector<std::pair<std::string, std::string>> kv;
    if (!ParseSimpleJSON(resp, kv)) return false;

    out_code = FindVal(kv, "code");
    out_session_id = FindVal(kv, "session_id");

    if (out_code.empty() || out_session_id.empty()) {
        std::string err = FindVal(kv, "error");
        Logger::Warning("DMClient PairCreate failed: " + (err.empty() ? resp : err));
        return false;
    }

    Logger::Info("DMClient PairCreate: code=" + out_code + " session=" + out_session_id);
    return true;
}

bool DMClient::PairJoin(const std::string& code, std::string& out_session_id,
                         std::string& out_peer_name) {
    std::string body = "{\"code\":\"" + JsonEscape(code) +
                       "\",\"user_id\":\"" + JsonEscape(user_id_) +
                       "\",\"display_name\":\"" + JsonEscape(display_name_) + "\"}";

    std::string resp = PostJSON("/dm/pair/join", body);
    if (resp.empty()) return false;

    std::vector<std::pair<std::string, std::string>> kv;
    if (!ParseSimpleJSON(resp, kv)) return false;

    out_session_id = FindVal(kv, "session_id");
    out_peer_name = FindVal(kv, "peer_name");

    if (out_session_id.empty()) {
        std::string err = FindVal(kv, "error");
        Logger::Warning("DMClient PairJoin failed: " + (err.empty() ? resp : err));
        return false;
    }

    Logger::Info("DMClient PairJoin: session=" + out_session_id + " peer=" + out_peer_name);
    return true;
}

bool DMClient::PairStatus(const std::string& session_id, std::string& out_status,
                           std::string& out_peer_name) {
    std::string url = "/dm/pair/status?session_id=" + session_id + "&user_id=" + user_id_;
    std::string resp = Get(url);
    if (resp.empty()) return false;

    std::vector<std::pair<std::string, std::string>> kv;
    if (!ParseSimpleJSON(resp, kv)) return false;

    out_status = FindVal(kv, "status");
    out_peer_name = FindVal(kv, "peer_name");
    return !out_status.empty();
}

bool DMClient::PairConfirm(const std::string& session_id) {
    std::string body = "{\"session_id\":\"" + JsonEscape(session_id) +
                       "\",\"user_id\":\"" + JsonEscape(user_id_) + "\"}";

    std::string resp = PostJSON("/dm/pair/confirm", body);
    if (resp.empty()) return false;

    std::vector<std::pair<std::string, std::string>> kv;
    if (!ParseSimpleJSON(resp, kv)) return false;

    std::string ok = FindVal(kv, "ok");
    return ok == "true";
}

// --- Messaging ---

bool DMClient::FetchMessages(const std::string& session_id, int64_t since) {
    std::string url = "/dm/messages?session_id=" + session_id +
                      "&since=" + std::to_string(since);
    std::string resp = Get(url);
    if (resp.empty()) return false;

    DMSession* session = GetSession(session_id);
    if (!session) return false;

    std::vector<DMMessage> msgs;
    if (!ParseMessages(resp, msgs)) {
        Logger::Warning("DMClient FetchMessages parse failed for " + session_id);
        return false;
    }

    session->messages = std::move(msgs);

    // Mark seen status and is_mine
    for (auto& msg : session->messages) {
        msg.is_mine = (msg.from_id == user_id_);
        msg.seen = (msg.date <= session->last_seen_date);
    }

    // Update unseen flag
    session->has_unseen = false;
    for (auto& msg : session->messages) {
        if (!msg.seen && !msg.is_mine) {
            session->has_unseen = true;
            break;
        }
    }

    return true;
}

bool DMClient::SendMessage(const std::string& session_id, const std::string& text) {
    std::string body = "{\"session_id\":\"" + JsonEscape(session_id) +
                       "\",\"user_id\":\"" + JsonEscape(user_id_) +
                       "\",\"text\":\"" + JsonEscape(text) + "\"}";

    std::string resp = PostJSON("/dm/send", body);
    if (resp.empty()) return false;

    std::vector<std::pair<std::string, std::string>> kv;
    if (!ParseSimpleJSON(resp, kv)) return false;

    return FindVal(kv, "ok") == "true";
}

// Parse message array: [{"from":"uid","from_name":"Alice","text":"hi","date":123}, ...]
bool DMClient::ParseMessages(const std::string& json, std::vector<DMMessage>& out) {
    out.clear();
    size_t pos = 0;
    while (pos < json.size() && json[pos] != '[') pos++;
    if (pos >= json.size()) return false;
    pos++;

    while (pos < json.size()) {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' ||
               json[pos] == '\r' || json[pos] == '\t' || json[pos] == ','))
            pos++;
        if (pos >= json.size() || json[pos] == ']') break;
        if (json[pos] != '{') return false;
        pos++;

        DMMessage msg;
        while (pos < json.size() && json[pos] != '}') {
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' ||
                   json[pos] == '\r' || json[pos] == '\t' || json[pos] == ','))
                pos++;
            if (pos >= json.size() || json[pos] == '}') break;

            if (json[pos] != '"') return false;
            pos++;
            size_t ks = pos;
            while (pos < json.size() && json[pos] != '"') pos++;
            if (pos >= json.size()) return false;
            std::string key = json.substr(ks, pos - ks);
            pos++;

            while (pos < json.size() && json[pos] != ':') pos++;
            if (pos >= json.size()) return false;
            pos++;
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

            if (json[pos] == '"') {
                pos++;
                std::string value;
                while (pos < json.size() && json[pos] != '"') {
                    if (json[pos] == '\\' && pos + 1 < json.size()) {
                        pos++;
                        switch (json[pos]) {
                            case '"': value += '"'; break;
                            case '\\': value += '\\'; break;
                            case 'n': value += '\n'; break;
                            case '/': value += '/'; break;
                            default: value += json[pos]; break;
                        }
                    } else {
                        value += json[pos];
                    }
                    pos++;
                }
                if (pos < json.size()) pos++;

                if (key == "from") msg.from_id = value;
                else if (key == "from_name") msg.from_name = value;
                else if (key == "text") msg.text = value;
            } else {
                size_t ns = pos;
                while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ' ')
                    pos++;
                std::string num = json.substr(ns, pos - ns);
                if (key == "date") {
                    try { msg.date = std::stoll(num); }
                    catch (...) { msg.date = 0; }
                }
            }
        }

        if (pos < json.size() && json[pos] == '}') pos++;
        out.push_back(std::move(msg));
    }

    // Sort newest first
    std::sort(out.begin(), out.end(),
              [](const DMMessage& a, const DMMessage& b) { return a.date > b.date; });

    return true;
}

// --- Session management ---

void DMClient::AddSession(const std::string& session_id, const std::string& peer_id,
                           const std::string& peer_name) {
    // Don't add duplicates
    for (auto& s : sessions_) {
        if (s.session_id == session_id) return;
    }
    DMSession s;
    s.session_id = session_id;
    s.peer_id = peer_id;
    s.peer_name = peer_name;
    sessions_.push_back(std::move(s));
}

void DMClient::RemoveSession(const std::string& session_id) {
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
                       [&](const DMSession& s) { return s.session_id == session_id; }),
        sessions_.end());
}

DMSession* DMClient::GetSession(const std::string& session_id) {
    for (auto& s : sessions_) {
        if (s.session_id == session_id) return &s;
    }
    return nullptr;
}

bool DMClient::HasUnseen() const {
    for (auto& s : sessions_) {
        if (s.has_unseen) return true;
    }
    return false;
}

void DMClient::MarkSessionSeen(const std::string& session_id, int64_t newest_date) {
    DMSession* s = GetSession(session_id);
    if (!s) return;
    s->last_seen_date = newest_date;
    for (auto& msg : s->messages) {
        msg.seen = true;
    }
    s->has_unseen = false;
}

void DMClient::PollAll() {
    for (auto& s : sessions_) {
        FetchMessages(s.session_id, 0);
    }
}

} // namespace YipOS
