#pragma once

#include <string>

namespace YipOS {

class SystemStats {
public:
    virtual ~SystemStats() = default;

    virtual void Update() = 0;

    virtual int GetCPUPercent() const = 0;
    virtual int GetCPUTemp() const = 0;
    virtual int GetMemPercent() const = 0;
    virtual std::string GetMemText() const = 0;
    virtual int GetGPUPercent() const = 0;
    virtual int GetGPUTemp() const = 0;
    virtual int GetDiskPercent() const = 0;
    virtual std::string GetDiskLabel() const = 0;
    virtual void CycleDisk() = 0;
    virtual std::string GetUptime() const = 0;

    static SystemStats* Create();
};

} // namespace YipOS
