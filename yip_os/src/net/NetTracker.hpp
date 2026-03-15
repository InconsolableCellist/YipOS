#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace YipOS {

class NetTracker {
public:
    NetTracker(const std::string& preferred_iface = "");

    void Sample();
    void CycleInterface();
    std::string SessionElapsed() const;

    static std::string FmtRate(double bps);
    static std::string FmtTotal(int64_t bytes);

    std::string iface;
    int64_t total_dl = 0;
    int64_t total_ul = 0;
    double current_dl = 0.0;
    double current_ul = 0.0;

private:
    struct NetBytes {
        std::string iface;
        int64_t rx = 0;
        int64_t tx = 0;
    };

    static std::vector<NetBytes> ReadAllInterfaces();
    const NetBytes* FindSelected(const std::vector<NetBytes>& interfaces) const;

    std::string selected_iface_;  // user-selected interface name (empty = auto)
    double session_start_;
    int64_t last_rx_ = 0;
    int64_t last_tx_ = 0;
    double last_time_ = 0;
    bool initialized_ = false;
};

} // namespace YipOS
