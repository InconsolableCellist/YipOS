#ifndef _WIN32

#include "AudioCapture.hpp"
#include "core/Logger.hpp"

namespace YipOS {

// Linux stub — audio capture not yet implemented
// TODO: PulseAudio/PipeWire monitor source capture
class LinuxCapture : public AudioCapture {
public:
    bool Start() override {
        Logger::Warning("CC: Audio capture not available on Linux yet");
        return false;
    }

    void Stop() override {}
    bool IsRunning() const override { return false; }

    std::vector<AudioDevice> GetDevices() override {
        return {};
    }

    void SetDevice(const std::string&) override {}
    std::string GetCurrentDeviceId() const override { return ""; }
    std::string GetCurrentDeviceName() const override { return "None (Linux stub)"; }
};

std::unique_ptr<AudioCapture> AudioCapture::Create() {
    return std::make_unique<LinuxCapture>();
}

} // namespace YipOS

#endif // !_WIN32
