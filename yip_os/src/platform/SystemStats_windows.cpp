#include "SystemStats.hpp"
#include "core/Logger.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdio>
#include <vector>
#include <string>
#include <chrono>

namespace YipOS {

class SystemStatsWindows : public SystemStats {
public:
    SystemStatsWindows() {
        GetSystemTimes(&prev_idle_, &prev_kernel_, &prev_user_);
        ScanDrives();
    }

    void Update() override {
        UpdateCPU();
        UpdateMemory();

        // GPU — nvidia-smi is slow, poll every 5s
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_gpu_poll_).count();
        if (elapsed >= 5.0 || gpu_first_) {
            UpdateGPU();
            last_gpu_poll_ = now;
            gpu_first_ = false;
        }

        UpdateDisk();
        UpdateUptime();
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
        if (drives_.size() <= 1) return;
        disk_index_ = (disk_index_ + 1) % drives_.size();
        UpdateDisk();
    }
    void SetDisk(const std::string& label) override {
        for (size_t i = 0; i < drives_.size(); i++) {
            std::string drive_label = drives_[i].substr(0, 2);
            if (drive_label == label) {
                disk_index_ = i;
                UpdateDisk();
                return;
            }
        }
    }
    std::string GetUptime() const override { return uptime_; }

private:
    // --- CPU ---
    void UpdateCPU() {
        FILETIME idle, kernel, user;
        if (!GetSystemTimes(&idle, &kernel, &user)) return;

        ULONGLONG d_idle = FTtoULL(idle) - FTtoULL(prev_idle_);
        ULONGLONG d_kernel = FTtoULL(kernel) - FTtoULL(prev_kernel_);
        ULONGLONG d_user = FTtoULL(user) - FTtoULL(prev_user_);
        ULONGLONG d_total = d_kernel + d_user; // kernel includes idle time

        cpu_pct_ = (d_total > 0) ? static_cast<int>(100 * (d_total - d_idle) / d_total) : 0;

        prev_idle_ = idle;
        prev_kernel_ = kernel;
        prev_user_ = user;

        // CPU temp: best-effort via WMI is heavy; skip for now
        cpu_temp_ = 0;
    }

    static ULONGLONG FTtoULL(const FILETIME& ft) {
        return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }

    // --- Memory ---
    void UpdateMemory() {
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        if (!GlobalMemoryStatusEx(&ms)) return;

        mem_pct_ = static_cast<int>(ms.dwMemoryLoad);
        double used_gb = (ms.ullTotalPhys - ms.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
        double total_gb = ms.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1f/%.0fG", used_gb, total_gb);
        mem_text_ = buf;
    }

    // --- GPU ---
    void UpdateGPU() {
        // nvidia-smi approach, same as Linux
        FILE* pipe = _popen("nvidia-smi --query-gpu=utilization.gpu,temperature.gpu --format=csv,noheader,nounits 2>NUL", "r");
        if (!pipe) { gpu_pct_ = 0; gpu_temp_ = 0; return; }
        char buf[64];
        if (fgets(buf, sizeof(buf), pipe)) {
            int pct = 0, temp = 0;
            if (std::sscanf(buf, "%d, %d", &pct, &temp) == 2) {
                gpu_pct_ = pct;
                gpu_temp_ = temp;
            }
        }
        _pclose(pipe);
    }

    // --- Disk ---
    void ScanDrives() {
        drives_.clear();
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; i++) {
            if (mask & (1 << i)) {
                char root[] = { static_cast<char>('A' + i), ':', '\\', '\0' };
                UINT type = GetDriveTypeA(root);
                if (type == DRIVE_FIXED) {
                    drives_.push_back(root);
                }
            }
        }
        if (drives_.empty()) {
            drives_.push_back("C:\\");
        }
        disk_index_ = 0;
        UpdateDisk();
    }

    void UpdateDisk() {
        const std::string& drive = drives_[disk_index_];
        ULARGE_INTEGER free_bytes, total_bytes;
        if (GetDiskFreeSpaceExA(drive.c_str(), &free_bytes, &total_bytes, nullptr)) {
            if (total_bytes.QuadPart > 0) {
                ULONGLONG used = total_bytes.QuadPart - free_bytes.QuadPart;
                disk_pct_ = static_cast<int>(100 * used / total_bytes.QuadPart);
            }
        }
        // Label: just the drive letter, e.g. "C:"
        disk_label_ = drive.substr(0, 2);
    }

    // --- Uptime ---
    void UpdateUptime() {
        ULONGLONG ms = GetTickCount64();
        int s = static_cast<int>(ms / 1000);
        int days = s / 86400;
        int hours = (s % 86400) / 3600;
        int mins = (s % 3600) / 60;
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%dd %dh %dm", days, hours, mins);
        uptime_ = buf;
    }

    // CPU state
    int cpu_pct_ = 0;
    int cpu_temp_ = 0;
    FILETIME prev_idle_{};
    FILETIME prev_kernel_{};
    FILETIME prev_user_{};

    // Memory state
    int mem_pct_ = 0;
    std::string mem_text_ = "0/0G";

    // GPU state
    int gpu_pct_ = 0;
    int gpu_temp_ = 0;
    std::chrono::steady_clock::time_point last_gpu_poll_{};
    bool gpu_first_ = true;

    // Disk state
    int disk_pct_ = 0;
    std::string disk_label_ = "C:";
    std::vector<std::string> drives_;
    size_t disk_index_ = 0;

    // Uptime
    std::string uptime_ = "0d 0h 0m";
};

SystemStats* SystemStats::Create() {
    return new SystemStatsWindows();
}

} // namespace YipOS
