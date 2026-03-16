#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>

namespace YipOS {

struct AudioDevice {
    std::string id;
    std::string name;
    bool is_loopback = false; // true = system output capture
};

// Thread-safe ring buffer for float32 PCM samples (16kHz mono)
class AudioRingBuffer {
public:
    explicit AudioRingBuffer(size_t capacity = 16000 * 30); // 30 seconds at 16kHz

    void Write(const float* data, size_t count);
    size_t Read(float* out, size_t max_count);
    size_t Available() const;
    void Clear();

private:
    std::vector<float> buffer_;
    size_t write_pos_ = 0;
    size_t read_pos_ = 0;
    size_t available_ = 0;
    mutable std::mutex mutex_;
};

// Abstract audio capture interface
// Platform implementations provide the actual capture logic
class AudioCapture {
public:
    virtual ~AudioCapture() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() const = 0;

    virtual std::vector<AudioDevice> GetDevices() = 0;
    virtual void SetDevice(const std::string& device_id) = 0;
    virtual std::string GetCurrentDeviceId() const = 0;
    virtual std::string GetCurrentDeviceName() const = 0;

    // Audio data flows into this ring buffer (16kHz mono float32)
    AudioRingBuffer& GetBuffer() { return buffer_; }

    static std::unique_ptr<AudioCapture> Create();

protected:
    AudioRingBuffer buffer_;
};

} // namespace YipOS
