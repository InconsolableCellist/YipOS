#include "NetTracker.hpp"
#include <chrono>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <algorithm>

namespace YipOS {

static double MonotonicNow() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

NetTracker::NetTracker() : session_start_(MonotonicNow()) {
    auto nb = ReadNetBytes();
    if (nb.valid) {
        iface = nb.iface;
        last_rx_ = nb.rx;
        last_tx_ = nb.tx;
        last_time_ = MonotonicNow();
        initialized_ = true;
    }
}

void NetTracker::Sample() {
    auto nb = ReadNetBytes();
    double now = MonotonicNow();
    if (nb.valid && initialized_) {
        double dt = now - last_time_;
        if (dt > 0) {
            int64_t dl = std::max(int64_t(0), nb.rx - last_rx_);
            int64_t ul = std::max(int64_t(0), nb.tx - last_tx_);
            current_dl = dl / dt;
            current_ul = ul / dt;
            total_dl += dl;
            total_ul += ul;
        }
    }
    if (nb.valid) {
        iface = nb.iface;
        last_rx_ = nb.rx;
        last_tx_ = nb.tx;
        last_time_ = now;
        initialized_ = true;
    }
}

std::string NetTracker::SessionElapsed() const {
    int s = static_cast<int>(MonotonicNow() - session_start_);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", s / 3600, (s % 3600) / 60, s % 60);
    return buf;
}

std::string NetTracker::FmtRate(double bps) {
    char buf[8];
    if (bps < 1000) {
        std::snprintf(buf, sizeof(buf), "%4.0fB", bps);
    } else {
        bps /= 1024;
        if (bps < 100) { std::snprintf(buf, sizeof(buf), "%4.1fk", bps); return buf; }
        if (bps < 1000) { std::snprintf(buf, sizeof(buf), "%4.0fk", bps); return buf; }
        bps /= 1024;
        if (bps < 100) { std::snprintf(buf, sizeof(buf), "%4.1fM", bps); return buf; }
        if (bps < 1000) { std::snprintf(buf, sizeof(buf), "%4.0fM", bps); return buf; }
        bps /= 1024;
        std::snprintf(buf, sizeof(buf), "%4.1fG", bps);
    }
    return buf;
}

std::string NetTracker::FmtTotal(int64_t n) {
    char buf[8];
    double v = static_cast<double>(n);
    if (v < 1000) { std::snprintf(buf, sizeof(buf), "%4.0fB", v); return buf; }
    v /= 1024;
    if (v < 100) { std::snprintf(buf, sizeof(buf), "%4.1fk", v); return buf; }
    if (v < 1000) { std::snprintf(buf, sizeof(buf), "%4.0fk", v); return buf; }
    v /= 1024;
    if (v < 100) { std::snprintf(buf, sizeof(buf), "%4.1fM", v); return buf; }
    if (v < 1000) { std::snprintf(buf, sizeof(buf), "%4.0fM", v); return buf; }
    v /= 1024;
    std::snprintf(buf, sizeof(buf), "%4.1fG", v);
    return buf;
}

NetTracker::NetBytes NetTracker::ReadNetBytes() {
#ifdef __linux__
    NetBytes best;
    std::ifstream f("/proc/net/dev");
    if (!f.is_open()) return best;

    std::string line;
    while (std::getline(f, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string name = line.substr(0, colon);
        // Trim whitespace
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t") + 1);

        if (name == "lo") continue;

        std::istringstream data(line.substr(colon + 1));
        int64_t rx, f1, f2, f3, f4, f5, f6, f7, tx;
        if (!(data >> rx >> f1 >> f2 >> f3 >> f4 >> f5 >> f6 >> f7 >> tx)) continue;

        if (!best.valid || rx + tx > best.rx + best.tx) {
            best = {name, rx, tx, true};
        }
    }
    return best;
#else
    return {};
#endif
}

} // namespace YipOS
