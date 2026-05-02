#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

typedef void CURL;

namespace YipOS {

class Config;

struct FurEvent {
    std::string id;            // stable id from API (or synthesized "title|start")
    std::string title;
    std::string description;
    std::string host;          // presenter / streamer / panel host
    std::string location;      // venue / world / stage
    std::string track;         // category if present
    std::string url;           // optional link
    int64_t start_unix = 0;    // start time in UTC seconds
    int64_t end_unix = 0;
    int day_index = 0;         // 0..day_count-1, derived from event-wide start
};

struct FurEventInfo {
    std::string name;          // e.g. "Furality Sylva"
    int64_t start_unix = 0;    // event-wide start (for computing day_index)
    int64_t end_unix = 0;
    int day_count = 0;
};

class FuralityClient {
public:
    FuralityClient();
    ~FuralityClient();

    // Hits both /v2/event and /v2/streamteam/schedule. Returns true on
    // partial or full success (events_ non-empty afterwards).
    bool FetchAll();

    const FurEventInfo& GetEventInfo() const { return info_; }
    const std::vector<FurEvent>& GetEvents() const { return events_; }
    int EventCountForDay(int day_index) const;
    std::vector<const FurEvent*> EventsForDay(int day_index) const;
    std::vector<const FurEvent*> AllEvents() const;
    std::vector<const FurEvent*> MarkedEvents() const;

    bool IsMarked(const std::string& id) const;
    // Persists immediately to Config (caller should Flush() to disk).
    void SetMarked(Config& cfg, const std::string& id, bool on);

    void LoadMarked(const Config& cfg);
    bool LoadCache(const std::string& path);
    bool SaveCache(const std::string& path) const;

    int64_t LastFetch() const { return last_fetch_; }
    bool HasData() const { return !events_.empty(); }

private:
    bool ParseEventInfo(const std::string& json);
    bool ParseSchedule(const std::string& json);
    bool HttpGet(const std::string& url, std::string& out_body);
    void RecomputeDayIndices();

    CURL* curl_ = nullptr;
    FurEventInfo info_;
    std::vector<FurEvent> events_;
    std::unordered_set<std::string> marked_;
    int64_t last_fetch_ = 0;
};

} // namespace YipOS
