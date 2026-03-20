#ifndef _WIN32

#include "AudioCapture.hpp"
#include "core/Logger.hpp"

#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <thread>
#include <atomic>

namespace YipOS {

class PulseCapture : public AudioCapture {
public:
    PulseCapture() = default;
    ~PulseCapture() override { Stop(); }

    bool Start() override {
        if (running_) return true;

        if (!InitDevice()) {
            Logger::Warning("CC: Failed to init PulseAudio capture");
            return false;
        }

        running_ = true;
        capture_thread_ = std::thread(&PulseCapture::CaptureLoop, this);
        Logger::Info("CC: Audio capture started");
        return true;
    }

    void Stop() override {
        running_ = false;
        if (capture_thread_.joinable())
            capture_thread_.join();
        ReleaseDevice();
        Logger::Info("CC: Audio capture stopped");
    }

    bool IsRunning() const override { return running_; }

    std::vector<AudioDevice> GetDevices() override {
        std::vector<AudioDevice> devices;

        pa_mainloop* ml = pa_mainloop_new();
        pa_mainloop_api* api = pa_mainloop_get_api(ml);
        pa_context* ctx = pa_context_new(api, "yip_os_enum");

        if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
            pa_context_unref(ctx);
            pa_mainloop_free(ml);
            return devices;
        }

        // Wait for context ready
        while (true) {
            pa_mainloop_iterate(ml, 1, nullptr);
            auto state = pa_context_get_state(ctx);
            if (state == PA_CONTEXT_READY) break;
            if (!PA_CONTEXT_IS_GOOD(state)) {
                pa_context_unref(ctx);
                pa_mainloop_free(ml);
                return devices;
            }
        }

        // Enumerate sources (mics + monitor/loopback)
        struct EnumData {
            std::vector<AudioDevice>* devices;
            bool done;
        } enum_data{&devices, false};

        auto source_cb = [](pa_context*, const pa_source_info* info, int eol, void* userdata) {
            auto* ed = static_cast<EnumData*>(userdata);
            if (eol > 0) { ed->done = true; return; }
            if (!info) return;

            AudioDevice ad;
            ad.id = info->name;
            ad.name = info->description ? info->description : info->name;
            // Monitor sources are loopback (system audio capture)
            ad.is_loopback = (info->monitor_of_sink != PA_INVALID_INDEX);
            if (ad.is_loopback) ad.name += " [Loopback]";
            ed->devices->push_back(std::move(ad));
        };

        pa_context_get_source_info_list(ctx, source_cb, &enum_data);
        while (!enum_data.done)
            pa_mainloop_iterate(ml, 1, nullptr);

        pa_context_disconnect(ctx);
        pa_context_unref(ctx);
        pa_mainloop_free(ml);
        return devices;
    }

    void SetDevice(const std::string& device_id) override {
        bool was_running = running_;
        if (was_running) Stop();
        selected_device_id_ = device_id;

        // Determine if loopback by checking source list
        auto devices = GetDevices();
        selected_is_loopback_ = false;
        current_device_name_ = device_id;
        for (auto& d : devices) {
            if (d.id == device_id) {
                current_device_name_ = d.name;
                selected_is_loopback_ = d.is_loopback;
                break;
            }
        }

        if (was_running) Start();
    }

    std::string GetCurrentDeviceId() const override {
        return selected_device_id_;
    }

    std::string GetCurrentDeviceName() const override {
        return current_device_name_;
    }

private:
    bool InitDevice() {
        // If no device selected, find default monitor source (loopback)
        if (selected_device_id_.empty()) {
            auto devices = GetDevices();
            for (auto& d : devices) {
                if (d.is_loopback) {
                    selected_device_id_ = d.id;
                    current_device_name_ = d.name;
                    selected_is_loopback_ = true;
                    break;
                }
            }
            if (selected_device_id_.empty() && !devices.empty()) {
                selected_device_id_ = devices[0].id;
                current_device_name_ = devices[0].name;
                selected_is_loopback_ = devices[0].is_loopback;
            }
            if (selected_device_id_.empty()) {
                Logger::Warning("CC: No audio sources found");
                return false;
            }
        }

        // Open a pa_simple recording stream on the selected source
        // Capture at native 44100 or 48000 stereo, then resample in the loop
        pa_sample_spec spec;
        spec.format = PA_SAMPLE_FLOAT32LE;
        spec.rate = 48000;
        spec.channels = 2;

        int error = 0;
        simple_ = pa_simple_new(
            nullptr,                      // default server
            "yip_os",                     // app name
            PA_STREAM_RECORD,             // direction
            selected_device_id_.c_str(),  // source name
            "CC Capture",                 // stream name
            &spec,                        // sample spec
            nullptr,                      // default channel map
            nullptr,                      // default buffering
            &error
        );

        if (!simple_) {
            Logger::Warning("CC: pa_simple_new failed: " + std::string(pa_strerror(error)));
            return false;
        }

        source_sample_rate_ = spec.rate;
        source_channels_ = spec.channels;

        Logger::Info("CC: PulseAudio device opened: " + current_device_name_ +
                     " (" + std::to_string(source_sample_rate_) + "Hz, " +
                     std::to_string(source_channels_) + "ch)");
        return true;
    }

    void ReleaseDevice() {
        if (simple_) {
            pa_simple_free(simple_);
            simple_ = nullptr;
        }
    }

    void CaptureLoop() {
        // Resample from source rate to 16kHz mono
        double resample_ratio = 16000.0 / source_sample_rate_;
        double resample_accum = 0.0;

        // Read buffer: 10ms of stereo float32 at source rate
        const int frames_per_read = source_sample_rate_ / 100;
        const int samples_per_read = frames_per_read * source_channels_;
        std::vector<float> read_buf(samples_per_read);

        while (running_) {
            int error = 0;
            int ret = pa_simple_read(simple_, read_buf.data(),
                                     samples_per_read * sizeof(float), &error);
            if (ret < 0) {
                if (running_)
                    Logger::Warning("CC: pa_simple_read failed: " + std::string(pa_strerror(error)));
                break;
            }

            for (int i = 0; i < frames_per_read; i++) {
                // Mix to mono
                float mono = 0.0f;
                for (int ch = 0; ch < source_channels_; ch++)
                    mono += read_buf[i * source_channels_ + ch];
                mono /= source_channels_;

                // Simple resampling via accumulator
                resample_accum += resample_ratio;
                if (resample_accum >= 1.0) {
                    resample_accum -= 1.0;
                    buffer_.Write(&mono, 1);
                }
            }
        }
    }

    std::thread capture_thread_;
    std::atomic<bool> running_{false};

    std::string selected_device_id_;
    std::string current_device_name_ = "None";
    bool selected_is_loopback_ = false;

    pa_simple* simple_ = nullptr;
    int source_sample_rate_ = 48000;
    int source_channels_ = 2;
};

std::unique_ptr<AudioCapture> AudioCapture::Create() {
    return std::make_unique<PulseCapture>();
}

} // namespace YipOS

#endif // !_WIN32
