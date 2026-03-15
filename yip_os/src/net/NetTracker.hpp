#pragma once

#include <string>
#include <cstdint>

namespace YipOS {

class NetTracker {
public:
    NetTracker();

    void Sample();
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
        bool valid = false;
    };

    static NetBytes ReadNetBytes();

    double session_start_;
    int64_t last_rx_ = 0;
    int64_t last_tx_ = 0;
    double last_time_ = 0;
    bool initialized_ = false;
};

} // namespace YipOS
