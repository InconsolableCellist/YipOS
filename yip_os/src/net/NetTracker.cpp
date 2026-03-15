#include "NetTracker.hpp"
#include <chrono>
#include <cstdio>
#include <algorithm>

#ifdef __linux__
#include <fstream>
#include <sstream>
#elif defined(_WIN32)
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#pragma comment(lib, "iphlpapi.lib")
#endif

namespace YipOS {

static double MonotonicNow() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

NetTracker::NetTracker(const std::string& preferred_iface) : session_start_(MonotonicNow()) {
    auto interfaces = ReadAllInterfaces();
    if (!interfaces.empty()) {
        // Try preferred interface first
        const NetBytes* pick = nullptr;
        if (!preferred_iface.empty()) {
            for (auto& nb : interfaces) {
                if (nb.iface == preferred_iface) { pick = &nb; break; }
            }
        }
        // Fall back to interface with highest traffic
        if (!pick) {
            auto best = std::max_element(interfaces.begin(), interfaces.end(),
                [](const NetBytes& a, const NetBytes& b) { return a.rx + a.tx < b.rx + b.tx; });
            pick = &(*best);
        }
        selected_iface_ = pick->iface;
        iface = selected_iface_;
        last_rx_ = pick->rx;
        last_tx_ = pick->tx;
        last_time_ = MonotonicNow();
        initialized_ = true;
    }
}

const NetTracker::NetBytes* NetTracker::FindSelected(const std::vector<NetBytes>& interfaces) const {
    for (auto& nb : interfaces) {
        if (nb.iface == selected_iface_) return &nb;
    }
    return nullptr;
}

void NetTracker::CycleInterface() {
    auto interfaces = ReadAllInterfaces();
    if (interfaces.size() <= 1) return;

    // Find current index, advance to next
    for (size_t i = 0; i < interfaces.size(); i++) {
        if (interfaces[i].iface == selected_iface_) {
            size_t next = (i + 1) % interfaces.size();
            selected_iface_ = interfaces[next].iface;
            iface = selected_iface_;
            // Reset counters for new interface
            last_rx_ = interfaces[next].rx;
            last_tx_ = interfaces[next].tx;
            last_time_ = MonotonicNow();
            total_dl = 0;
            total_ul = 0;
            current_dl = 0;
            current_ul = 0;
            return;
        }
    }
    // Current not found, pick first
    selected_iface_ = interfaces[0].iface;
    iface = selected_iface_;
    last_rx_ = interfaces[0].rx;
    last_tx_ = interfaces[0].tx;
    last_time_ = MonotonicNow();
    total_dl = 0;
    total_ul = 0;
    current_dl = 0;
    current_ul = 0;
}

void NetTracker::Sample() {
    auto interfaces = ReadAllInterfaces();
    double now = MonotonicNow();

    auto* nb = FindSelected(interfaces);
    if (!nb && !interfaces.empty()) {
        // Selected interface disappeared, pick best
        auto best = std::max_element(interfaces.begin(), interfaces.end(),
            [](const NetBytes& a, const NetBytes& b) { return a.rx + a.tx < b.rx + b.tx; });
        selected_iface_ = best->iface;
        nb = &(*best);
    }

    if (nb && initialized_) {
        double dt = now - last_time_;
        if (dt > 0) {
            int64_t dl = std::max(int64_t(0), nb->rx - last_rx_);
            int64_t ul = std::max(int64_t(0), nb->tx - last_tx_);
            current_dl = dl / dt;
            current_ul = ul / dt;
            total_dl += dl;
            total_ul += ul;
        }
    }
    if (nb) {
        iface = nb->iface;
        last_rx_ = nb->rx;
        last_tx_ = nb->tx;
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

std::vector<NetTracker::NetBytes> NetTracker::ReadAllInterfaces() {
    std::vector<NetBytes> result;

#ifdef __linux__
    std::ifstream f("/proc/net/dev");
    if (!f.is_open()) return result;

    std::string line;
    while (std::getline(f, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string name = line.substr(0, colon);
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t") + 1);

        if (name == "lo") continue;

        std::istringstream data(line.substr(colon + 1));
        int64_t rx, f1, f2, f3, f4, f5, f6, f7, tx;
        if (!(data >> rx >> f1 >> f2 >> f3 >> f4 >> f5 >> f6 >> f7 >> tx)) continue;

        result.push_back({name, rx, tx});
    }

#elif defined(_WIN32)
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) == NO_ERROR && table) {
        for (ULONG i = 0; i < table->NumEntries; i++) {
            auto& row = table->Table[i];
            // Skip loopback and interfaces that are not up
            if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            if (row.OperStatus != IfOperStatusUp) continue;

            // Convert wide alias to narrow string
            char name[64];
            int len = WideCharToMultiByte(CP_UTF8, 0, row.Alias, -1, name, sizeof(name), nullptr, nullptr);
            if (len <= 0) continue;

            result.push_back({name, static_cast<int64_t>(row.InOctets), static_cast<int64_t>(row.OutOctets)});
        }
        FreeMibTable(table);
    }
#endif

    return result;
}

} // namespace YipOS
