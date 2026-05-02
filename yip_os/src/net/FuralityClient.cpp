#include "FuralityClient.hpp"

#include "core/Config.hpp"
#include "core/Logger.hpp"

#define NOMINMAX
#include <nlohmann/json.hpp>
#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace YipOS {

namespace {

constexpr const char* kEventURL    = "https://api.fynn.ai/v2/event";
constexpr const char* kScheduleURL = "https://api.fynn.ai/v2/streamteam/schedule";

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

// Try ISO-8601 with optional Z/offset, then epoch seconds, then epoch ms.
int64_t ParseTime(const nlohmann::json& v) {
    if (v.is_null()) return 0;
    if (v.is_number_integer() || v.is_number_unsigned()) {
        int64_t n = v.get<int64_t>();
        // Heuristic: > 10^12 implies milliseconds since epoch.
        return (n > 1'000'000'000'000LL) ? n / 1000 : n;
    }
    if (v.is_number_float()) {
        double n = v.get<double>();
        return static_cast<int64_t>(n > 1e12 ? n / 1000.0 : n);
    }
    if (!v.is_string()) return 0;

    std::string s = v.get<std::string>();
    if (s.empty()) return 0;

    // Try ISO-8601: 2024-03-21T17:00:00Z or with offset
    std::tm tm{};
    std::istringstream iss(s);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (iss.fail()) {
        // Try a date-only or alternate format
        iss.clear();
        iss.str(s);
        iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        if (iss.fail()) {
            iss.clear();
            iss.str(s);
            iss >> std::get_time(&tm, "%Y-%m-%d");
            if (iss.fail()) return 0;
        }
    }

    // Extract trailing offset if present (e.g. "+00:00" or "-04:00" or "Z")
    int offset_secs = 0;
    if (!s.empty()) {
        size_t pos = s.find_last_of("+-Z");
        if (pos != std::string::npos && pos > 10) {
            char c = s[pos];
            if (c == 'Z') {
                offset_secs = 0;
            } else {
                std::string off = s.substr(pos);
                int sign = (c == '+') ? 1 : -1;
                int oh = 0, om = 0;
                if (off.size() >= 5) {  // +HH:MM or +HHMM
                    try {
                        oh = std::stoi(off.substr(1, 2));
                        size_t mp = (off[3] == ':') ? 4 : 3;
                        if (off.size() >= mp + 2) om = std::stoi(off.substr(mp, 2));
                    } catch (...) {}
                }
                offset_secs = sign * (oh * 3600 + om * 60);
            }
        }
    }

#if defined(_WIN32)
    int64_t t = static_cast<int64_t>(_mkgmtime(&tm));
#else
    int64_t t = static_cast<int64_t>(timegm(&tm));
#endif
    if (t <= 0) return 0;
    return t - offset_secs;
}

std::string GetStr(const nlohmann::json& obj,
                   std::initializer_list<const char*> keys) {
    for (auto* k : keys) {
        auto it = obj.find(k);
        if (it != obj.end() && it->is_string()) return it->get<std::string>();
    }
    return {};
}

const nlohmann::json* GetField(const nlohmann::json& obj,
                                std::initializer_list<const char*> keys) {
    for (auto* k : keys) {
        auto it = obj.find(k);
        if (it != obj.end()) return &(*it);
    }
    return nullptr;
}

void ParseSingleEvent(const nlohmann::json& j, FurEvent& ev) {
    ev.id          = GetStr(j, {"id", "_id", "uuid", "slug"});
    ev.title       = GetStr(j, {"title", "name", "summary", "subject"});
    ev.description = GetStr(j, {"description", "desc", "details", "summary_long", "body", "abstract"});
    ev.host        = GetStr(j, {"host", "presenter", "streamer", "performer", "user", "presenters", "hosts", "channel", "creator"});
    ev.location    = GetStr(j, {"location", "venue", "world", "stage", "room", "place"});
    ev.track       = GetStr(j, {"track", "category", "type", "tag", "kind"});
    ev.url         = GetStr(j, {"url", "link", "twitch_url", "stream_url", "channel_url"});

    if (auto* s = GetField(j, {"start_time", "startTime", "start", "starts_at", "start_at", "scheduled_at", "scheduledAt", "time", "begin", "beginAt"}))
        ev.start_unix = ParseTime(*s);
    if (auto* e = GetField(j, {"end_time", "endTime", "end", "ends_at", "end_at", "finish", "finishAt"}))
        ev.end_unix = ParseTime(*e);

    if (ev.id.empty()) {
        ev.id = ev.title + "|" + std::to_string(ev.start_unix);
    }
}

// Heuristic: does this object look like a schedule entry?
bool LooksLikeEvent(const nlohmann::json& j) {
    if (!j.is_object()) return false;
    bool has_title_like =
        j.contains("title") || j.contains("name") || j.contains("summary") ||
        j.contains("subject");
    bool has_time_like =
        j.contains("start_time") || j.contains("startTime") || j.contains("start") ||
        j.contains("starts_at") || j.contains("start_at") || j.contains("scheduled_at") ||
        j.contains("scheduledAt") || j.contains("time") || j.contains("begin");
    return has_title_like && has_time_like;
}

// Walk the JSON tree and return the first array whose elements look like
// schedule entries, or nullptr if none. Handles arbitrary wrapper shapes
// like {"data": {"items": [...]}} without us needing to know the exact path.
const nlohmann::json* FindEventArray(const nlohmann::json& j) {
    if (j.is_array()) {
        if (!j.empty() && LooksLikeEvent(j[0])) return &j;
    }
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (auto* found = FindEventArray(*it)) return found;
        }
    }
    if (j.is_array()) {
        for (auto& el : j) {
            if (auto* found = FindEventArray(el)) return found;
        }
    }
    return nullptr;
}

} // namespace

FuralityClient::FuralityClient() {
    curl_ = curl_easy_init();
}

FuralityClient::~FuralityClient() {
    if (curl_) curl_easy_cleanup(curl_);
}

bool FuralityClient::HttpGet(const std::string& url, std::string& out_body) {
    if (!curl_) return false;
    out_body.clear();

    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &out_body);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_, CURLOPT_USERAGENT, "YipOS/1.0");
    curl_easy_setopt(curl_, CURLOPT_ACCEPT_ENCODING, "");

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        Logger::Warning(std::string("FuralityClient ") + url + " failed: " +
                        curl_easy_strerror(res));
        return false;
    }
    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        Logger::Warning("FuralityClient HTTP " + std::to_string(http_code) +
                        " for " + url);
        return false;
    }
    return true;
}

bool FuralityClient::FetchAll() {
    std::string body;
    bool got_info = false;
    if (HttpGet(kEventURL, body)) {
        got_info = ParseEventInfo(body);
    }

    if (HttpGet(kScheduleURL, body)) {
        if (!ParseSchedule(body)) {
            Logger::Warning("FuralityClient: schedule parse failed");
        }
    }

    RecomputeDayIndices();

    last_fetch_ = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    Logger::Info("FuralityClient: " + std::to_string(events_.size()) +
                 " event(s), " + std::to_string(info_.day_count) + " day(s)");
    return got_info || !events_.empty();
}

bool FuralityClient::ParseEventInfo(const std::string& body) {
    try {
        auto j = nlohmann::json::parse(body);
        // The API may return either a top-level object or a wrapper like
        // {"data": {...}} / {"event": {...}}.
        const nlohmann::json* root = &j;
        if (auto* d = GetField(j, {"data", "event", "result"})) root = d;
        if (root->is_array() && !root->empty()) root = &(*root)[0];

        info_.name = GetStr(*root, {"name", "title"});
        if (auto* s = GetField(*root, {"start_time", "startTime", "start", "starts_at"}))
            info_.start_unix = ParseTime(*s);
        if (auto* e = GetField(*root, {"end_time", "endTime", "end", "ends_at"}))
            info_.end_unix = ParseTime(*e);

        if (info_.start_unix > 0 && info_.end_unix > info_.start_unix) {
            int64_t span = info_.end_unix - info_.start_unix;
            info_.day_count = static_cast<int>((span + 86399) / 86400);
            if (info_.day_count < 1) info_.day_count = 1;
        }
        return true;
    } catch (const std::exception& e) {
        Logger::Warning(std::string("FuralityClient event parse: ") + e.what());
        return false;
    }
}

bool FuralityClient::ParseSchedule(const std::string& body) {
    try {
        auto j = nlohmann::json::parse(body);

        // Try the conventional wrapper keys first…
        const nlohmann::json* arr = nullptr;
        if (j.is_array()) {
            arr = &j;
        } else if (j.is_object()) {
            if (auto* d = GetField(j, {"data", "schedule", "events", "items",
                                       "results", "stream", "streams", "shows"})) {
                if (d->is_array()) {
                    arr = d;
                } else if (d->is_object()) {
                    // One more level of nesting (e.g. {"data":{"events":[…]}})
                    if (auto* dd = GetField(*d, {"items", "events", "schedule",
                                                  "results", "data"})) {
                        if (dd->is_array()) arr = dd;
                    }
                }
            }
        }

        // …and if those didn't pan out, recursively hunt for any array of
        // event-shaped objects.
        if (!arr) arr = FindEventArray(j);

        if (!arr || !arr->is_array()) {
            Logger::Warning("FuralityClient: schedule body has no recognizable "
                            "event array. Snippet: " + body.substr(0, 400));
            return false;
        }

        events_.clear();
        events_.reserve(arr->size());
        int rejected = 0;
        for (auto& item : *arr) {
            if (!item.is_object()) { rejected++; continue; }
            FurEvent ev;
            ParseSingleEvent(item, ev);
            if (ev.title.empty() && ev.start_unix == 0) { rejected++; continue; }
            events_.push_back(std::move(ev));
        }

        if (events_.empty()) {
            // Dump a small sample so we can see the actual field names
            std::string sample;
            if (!arr->empty()) sample = (*arr)[0].dump().substr(0, 600);
            Logger::Warning("FuralityClient: parsed " + std::to_string(arr->size()) +
                            " entries but extracted 0 events (rejected " +
                            std::to_string(rejected) + "). Sample item: " + sample);
        }

        std::sort(events_.begin(), events_.end(),
                  [](const FurEvent& a, const FurEvent& b) {
                      return a.start_unix < b.start_unix;
                  });
        return !events_.empty();
    } catch (const std::exception& e) {
        Logger::Warning(std::string("FuralityClient schedule parse: ") + e.what() +
                        " body[0..200]: " + body.substr(0, 200));
        return false;
    }
}

void FuralityClient::RecomputeDayIndices() {
    if (info_.start_unix <= 0) {
        // Fall back: derive from earliest event start
        if (events_.empty()) {
            info_.day_count = 0;
            return;
        }
        info_.start_unix = events_.front().start_unix;
        if (events_.back().end_unix > 0)
            info_.end_unix = events_.back().end_unix;
    }

    // Anchor on the local-midnight of the start day so events on the
    // same calendar day group together.
    std::time_t start_t = static_cast<std::time_t>(info_.start_unix);
    std::tm* lt = std::localtime(&start_t);
    if (lt) {
        lt->tm_hour = 0;
        lt->tm_min = 0;
        lt->tm_sec = 0;
        info_.start_unix = static_cast<int64_t>(std::mktime(lt));
    }

    int max_day = 0;
    for (auto& e : events_) {
        if (e.start_unix <= 0) continue;
        int64_t off = e.start_unix - info_.start_unix;
        if (off < 0) off = 0;
        e.day_index = static_cast<int>(off / 86400);
        if (e.day_index > max_day) max_day = e.day_index;
    }
    if (info_.day_count == 0) info_.day_count = max_day + 1;
}

int FuralityClient::EventCountForDay(int day_index) const {
    int n = 0;
    for (auto& e : events_) {
        if (e.day_index == day_index) n++;
    }
    return n;
}

std::vector<const FurEvent*> FuralityClient::EventsForDay(int day_index) const {
    std::vector<const FurEvent*> out;
    for (auto& e : events_) {
        if (e.day_index == day_index) out.push_back(&e);
    }
    return out;
}

std::vector<const FurEvent*> FuralityClient::AllEvents() const {
    std::vector<const FurEvent*> out;
    out.reserve(events_.size());
    for (auto& e : events_) out.push_back(&e);
    return out;
}

std::vector<const FurEvent*> FuralityClient::MarkedEvents() const {
    std::vector<const FurEvent*> out;
    for (auto& e : events_) {
        if (marked_.count(e.id)) out.push_back(&e);
    }
    return out;
}

bool FuralityClient::IsMarked(const std::string& id) const {
    return marked_.count(id) > 0;
}

void FuralityClient::SetMarked(Config& cfg, const std::string& id, bool on) {
    if (on) marked_.insert(id);
    else    marked_.erase(id);

    std::string serialized;
    bool first = true;
    for (auto& m : marked_) {
        if (!first) serialized += '\x1F';  // unit separator — ids may contain commas
        serialized += m;
        first = false;
    }
    cfg.SetState("fur.marked", serialized);
}

void FuralityClient::LoadMarked(const Config& cfg) {
    marked_.clear();
    std::string s = cfg.GetState("fur.marked");
    if (s.empty()) return;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find('\x1F', start);
        if (end == std::string::npos) end = s.size();
        std::string id = s.substr(start, end - start);
        if (!id.empty()) marked_.insert(id);
        if (end == s.size()) break;
        start = end + 1;
    }
}

bool FuralityClient::LoadCache(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string body = ss.str();
    if (body.empty()) return false;

    try {
        auto j = nlohmann::json::parse(body);
        info_.name       = j.value("name", "");
        info_.start_unix = j.value("start_unix", int64_t{0});
        info_.end_unix   = j.value("end_unix", int64_t{0});
        info_.day_count  = j.value("day_count", 0);
        last_fetch_      = j.value("fetched_at", int64_t{0});

        events_.clear();
        if (j.contains("events") && j["events"].is_array()) {
            for (auto& e : j["events"]) {
                FurEvent ev;
                ev.id          = e.value("id", "");
                ev.title       = e.value("title", "");
                ev.description = e.value("description", "");
                ev.host        = e.value("host", "");
                ev.location    = e.value("location", "");
                ev.track       = e.value("track", "");
                ev.url         = e.value("url", "");
                ev.start_unix  = e.value("start_unix", int64_t{0});
                ev.end_unix    = e.value("end_unix", int64_t{0});
                ev.day_index   = e.value("day_index", 0);
                if (!ev.id.empty()) events_.push_back(std::move(ev));
            }
        }
        Logger::Info("FuralityClient: loaded " + std::to_string(events_.size()) +
                     " event(s) from cache");
        return !events_.empty();
    } catch (const std::exception& e) {
        Logger::Warning(std::string("FuralityClient cache parse: ") + e.what());
        return false;
    }
}

bool FuralityClient::SaveCache(const std::string& path) const {
    nlohmann::json j;
    j["name"]       = info_.name;
    j["start_unix"] = info_.start_unix;
    j["end_unix"]   = info_.end_unix;
    j["day_count"]  = info_.day_count;
    j["fetched_at"] = last_fetch_;

    auto arr = nlohmann::json::array();
    for (auto& e : events_) {
        nlohmann::json je;
        je["id"]          = e.id;
        je["title"]       = e.title;
        je["description"] = e.description;
        je["host"]        = e.host;
        je["location"]    = e.location;
        je["track"]       = e.track;
        je["url"]         = e.url;
        je["start_unix"]  = e.start_unix;
        je["end_unix"]    = e.end_unix;
        je["day_index"]   = e.day_index;
        arr.push_back(std::move(je));
    }
    j["events"] = std::move(arr);

    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::ofstream f(path);
    if (!f.is_open()) {
        Logger::Warning("FuralityClient: cannot write cache " + path);
        return false;
    }
    f << j.dump();
    return true;
}

} // namespace YipOS
