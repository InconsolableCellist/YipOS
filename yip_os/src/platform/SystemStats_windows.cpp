#include "SystemStats.hpp"

namespace YipOS {

class SystemStatsWindows : public SystemStats {
public:
    void Update() override {}
    int GetCPUPercent() const override { return 0; }
    int GetCPUTemp() const override { return 0; }
    int GetMemPercent() const override { return 0; }
    std::string GetMemText() const override { return "0/0G"; }
    int GetGPUPercent() const override { return 0; }
    int GetGPUTemp() const override { return 0; }
    int GetDiskPercent() const override { return 0; }
    std::string GetDiskLabel() const override { return "/"; }
    void CycleDisk() override {}
    std::string GetUptime() const override { return "0d 0h 0m"; }
};

SystemStats* SystemStats::Create() {
    return new SystemStatsWindows();
}

} // namespace YipOS
