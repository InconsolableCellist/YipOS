#include "SystemStats.hpp"
#include "core/Logger.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <array>
#include <chrono>
#include <vector>
#include <sys/statvfs.h>

namespace YipOS {

class SystemStatsLinux : public SystemStats {
public:
    SystemStatsLinux() {
        ReadCPUTimes(prev_idle_, prev_total_);
        ScanMountPoints();
    }

    void Update() override {
        // CPU
        long long idle, total;
        ReadCPUTimes(idle, total);
        long long d_idle = idle - prev_idle_;
        long long d_total = total - prev_total_;
        cpu_pct_ = (d_total > 0) ? static_cast<int>(100 * (d_total - d_idle) / d_total) : 0;
        prev_idle_ = idle;
        prev_total_ = total;

        // CPU temp
        cpu_temp_ = ReadThermalZone();

        // Memory
        ReadMemInfo();

        // GPU — nvidia-smi is slow (shell exec), so only poll every 5s
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_gpu_poll_).count();
        if (elapsed >= 5.0 || gpu_first_) {
            ReadGPU();
            last_gpu_poll_ = now;
            gpu_first_ = false;
        }

        // Disk
        ReadDisk();

        // Uptime
        ReadUptime();
    }

    int GetCPUPercent() const override { return cpu_pct_; }
    int GetCPUTemp() const override { return cpu_temp_; }
    int GetMemPercent() const override { return mem_pct_; }
    std::string GetMemText() const override { return mem_text_; }
    int GetGPUPercent() const override { return gpu_pct_; }
    int GetGPUTemp() const override { return gpu_temp_; }
    int GetDiskPercent() const override { return disk_pct_; }
    std::string GetDiskLabel() const override { return disk_label_; }
    void CycleDisk() override {
        if (mount_points_.size() <= 1) return;
        disk_index_ = (disk_index_ + 1) % mount_points_.size();
        ReadDisk();
    }
    void SetDisk(const std::string& label) override {
        for (size_t i = 0; i < mount_points_.size(); i++) {
            const auto& m = mount_points_[i];
            std::string short_label;
            if (m == "/") {
                short_label = "/";
            } else {
                auto pos = m.rfind('/');
                short_label = (pos != std::string::npos && pos + 1 < m.size())
                              ? m.substr(pos + 1) : m;
            }
            if (short_label.size() > 4) short_label = short_label.substr(0, 4);
            if (short_label == label) {
                disk_index_ = i;
                ReadDisk();
                return;
            }
        }
    }
    std::string GetUptime() const override { return uptime_; }

private:
    static void ReadCPUTimes(long long& idle, long long& total) {
        std::ifstream f("/proc/stat");
        std::string line;
        if (std::getline(f, line) && line.substr(0, 3) == "cpu") {
            std::istringstream ss(line.substr(5));
            long long user, nice, system, idle_val, iowait, irq, softirq, steal;
            ss >> user >> nice >> system >> idle_val >> iowait >> irq >> softirq >> steal;
            idle = idle_val + iowait;
            total = user + nice + system + idle_val + iowait + irq + softirq + steal;
        } else {
            idle = total = 0;
        }
    }

    static int ReadThermalZone() {
        std::ifstream f("/sys/class/thermal/thermal_zone0/temp");
        int temp = 0;
        if (f >> temp) return temp / 1000;
        return 0;
    }

    void ReadMemInfo() {
        std::ifstream f("/proc/meminfo");
        std::string line;
        long long total = 0, available = 0;
        while (std::getline(f, line)) {
            if (line.compare(0, 9, "MemTotal:") == 0) {
                std::sscanf(line.c_str(), "MemTotal: %lld", &total);
            } else if (line.compare(0, 13, "MemAvailable:") == 0) {
                std::sscanf(line.c_str(), "MemAvailable: %lld", &available);
            }
        }
        if (total > 0) {
            long long used = total - available;
            mem_pct_ = static_cast<int>(100 * used / total);
            double used_gb = used / 1048576.0;
            double total_gb = total / 1048576.0;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f/%.0fG", used_gb, total_gb);
            mem_text_ = buf;
        }
    }

    void ReadGPU() {
        // Best-effort: try nvidia-smi
        FILE* pipe = popen("nvidia-smi --query-gpu=utilization.gpu,temperature.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
        if (!pipe) { gpu_pct_ = 0; gpu_temp_ = 0; return; }
        char buf[64];
        if (fgets(buf, sizeof(buf), pipe)) {
            int pct = 0, temp = 0;
            if (std::sscanf(buf, "%d, %d", &pct, &temp) == 2) {
                gpu_pct_ = pct;
                gpu_temp_ = temp;
            }
        }
        pclose(pipe);
    }

    void ScanMountPoints() {
        mount_points_.clear();
        std::ifstream f("/proc/mounts");
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string dev, mount, fstype;
            ss >> dev >> mount >> fstype;
            // Filter to real block filesystems
            if (fstype == "ext4" || fstype == "btrfs" || fstype == "xfs" ||
                fstype == "ext3" || fstype == "ext2" || fstype == "f2fs" ||
                fstype == "ntfs" || fstype == "vfat" || fstype == "exfat" ||
                fstype == "zfs" || fstype == "bcachefs") {
                mount_points_.push_back(mount);
            }
        }
        if (mount_points_.empty()) {
            mount_points_.push_back("/");
        }
        disk_index_ = 0;
        ReadDisk();
    }

    void ReadDisk() {
        const std::string& mount = mount_points_[disk_index_];
        struct statvfs st;
        if (statvfs(mount.c_str(), &st) == 0) {
            unsigned long long total = st.f_blocks * st.f_frsize;
            unsigned long long avail = st.f_bavail * st.f_frsize;
            if (total > 0) {
                disk_pct_ = static_cast<int>(100 * (total - avail) / total);
            }
        }
        // Short label: use last path component, or "/" for root
        if (mount == "/") {
            disk_label_ = "/";
        } else {
            auto pos = mount.rfind('/');
            disk_label_ = (pos != std::string::npos && pos + 1 < mount.size())
                          ? mount.substr(pos + 1) : mount;
        }
        // Truncate to 4 chars for display
        if (disk_label_.size() > 4) {
            disk_label_ = disk_label_.substr(0, 4);
        }
    }

    void ReadUptime() {
        std::ifstream f("/proc/uptime");
        double secs = 0;
        if (f >> secs) {
            int s = static_cast<int>(secs);
            int days = s / 86400;
            int hours = (s % 86400) / 3600;
            int mins = (s % 3600) / 60;
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%dd %dh %dm", days, hours, mins);
            uptime_ = buf;
        }
    }

    int cpu_pct_ = 0;
    int cpu_temp_ = 0;
    int mem_pct_ = 0;
    std::string mem_text_ = "0/0G";
    int gpu_pct_ = 0;
    int gpu_temp_ = 0;
    int disk_pct_ = 0;
    std::string disk_label_ = "/";
    std::vector<std::string> mount_points_;
    size_t disk_index_ = 0;
    std::string uptime_ = "0d 0h 0m";

    long long prev_idle_ = 0;
    long long prev_total_ = 0;

    std::chrono::steady_clock::time_point last_gpu_poll_{};
    bool gpu_first_ = true;
};

SystemStats* SystemStats::Create() {
    return new SystemStatsLinux();
}

} // namespace YipOS
