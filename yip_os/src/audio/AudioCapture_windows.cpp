#ifdef _WIN32

#include "AudioCapture.hpp"
#include "core/Logger.hpp"

#include <Windows.h>
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <thread>

namespace YipOS {

class WASAPICapture : public AudioCapture {
public:
    WASAPICapture() = default;
    ~WASAPICapture() override { Stop(); }

    bool Start() override {
        if (running_) return true;

        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        co_initialized_ = SUCCEEDED(hr) || hr == S_FALSE;

        if (!InitDevice()) {
            Logger::Warning("CC: Failed to init audio device");
            return false;
        }

        running_ = true;
        capture_thread_ = std::thread(&WASAPICapture::CaptureLoop, this);
        Logger::Info("CC: Audio capture started");
        return true;
    }

    void Stop() override {
        running_ = false;
        if (capture_thread_.joinable())
            capture_thread_.join();
        ReleaseDevice();
        if (co_initialized_) {
            CoUninitialize();
            co_initialized_ = false;
        }
        Logger::Info("CC: Audio capture stopped");
    }

    bool IsRunning() const override { return running_; }

    std::vector<AudioDevice> GetDevices() override {
        std::vector<AudioDevice> devices;

        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool co_init = SUCCEEDED(hr) || hr == S_FALSE;

        IMMDeviceEnumerator* enumerator = nullptr;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                              CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                              reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr)) {
            if (co_init) CoUninitialize();
            return devices;
        }

        // Enumerate both render (loopback) and capture (mic) devices
        EDataFlow flows[] = { eRender, eCapture };
        for (auto flow : flows) {
            IMMDeviceCollection* collection = nullptr;
            hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
            if (FAILED(hr)) continue;

            UINT count = 0;
            collection->GetCount(&count);
            for (UINT i = 0; i < count; i++) {
                IMMDevice* device = nullptr;
                if (FAILED(collection->Item(i, &device))) continue;

                LPWSTR id = nullptr;
                device->GetId(&id);

                IPropertyStore* props = nullptr;
                device->OpenPropertyStore(STGM_READ, &props);
                PROPVARIANT name;
                PropVariantInit(&name);
                props->GetValue(PKEY_Device_FriendlyName, &name);

                AudioDevice ad;
                if (id) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, id, -1, nullptr, 0, nullptr, nullptr);
                    ad.id.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, id, -1, ad.id.data(), len, nullptr, nullptr);
                    CoTaskMemFree(id);
                }
                if (name.vt == VT_LPWSTR && name.pwszVal) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, name.pwszVal, -1, nullptr, 0, nullptr, nullptr);
                    ad.name.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, name.pwszVal, -1, ad.name.data(), len, nullptr, nullptr);
                }
                ad.is_loopback = (flow == eRender);
                if (ad.is_loopback) ad.name += " [Loopback]";

                PropVariantClear(&name);
                props->Release();
                device->Release();

                devices.push_back(std::move(ad));
            }
            collection->Release();
        }

        enumerator->Release();
        if (co_init) CoUninitialize();
        return devices;
    }

    void SetDevice(const std::string& device_id) override {
        bool was_running = running_;
        if (was_running) Stop();
        selected_device_id_ = device_id;
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
        IMMDeviceEnumerator* enumerator = nullptr;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                      CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                      reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr)) return false;

        IMMDevice* device = nullptr;
        bool is_loopback = false;

        if (selected_device_id_.empty()) {
            // Default render device (loopback)
            hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
            is_loopback = true;
            current_device_name_ = "System Default [Loopback]";
        } else {
            // Find selected device
            int wlen = MultiByteToWideChar(CP_UTF8, 0, selected_device_id_.c_str(), -1, nullptr, 0);
            std::wstring wid(wlen - 1, 0);
            MultiByteToWideChar(CP_UTF8, 0, selected_device_id_.c_str(), -1, wid.data(), wlen);
            hr = enumerator->GetDevice(wid.c_str(), &device);

            // Determine if this is a render device (loopback)
            IMMEndpoint* endpoint = nullptr;
            if (SUCCEEDED(device->QueryInterface(__uuidof(IMMEndpoint), reinterpret_cast<void**>(&endpoint)))) {
                EDataFlow flow;
                endpoint->GetDataFlow(&flow);
                is_loopback = (flow == eRender);
                endpoint->Release();
            }

            // Get friendly name
            IPropertyStore* props = nullptr;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
                PROPVARIANT name;
                PropVariantInit(&name);
                if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.pwszVal) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, name.pwszVal, -1, nullptr, 0, nullptr, nullptr);
                    current_device_name_.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, name.pwszVal, -1, current_device_name_.data(), len, nullptr, nullptr);
                }
                PropVariantClear(&name);
                props->Release();
            }
        }

        if (FAILED(hr) || !device) {
            enumerator->Release();
            return false;
        }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                               reinterpret_cast<void**>(&audio_client_));
        if (FAILED(hr)) {
            device->Release();
            enumerator->Release();
            return false;
        }

        WAVEFORMATEX* mix_format = nullptr;
        audio_client_->GetMixFormat(&mix_format);
        source_sample_rate_ = mix_format->nSamplesPerSec;
        source_channels_ = mix_format->nChannels;

        DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        if (is_loopback) stream_flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;

        REFERENCE_TIME buffer_duration = 100000; // 10ms
        hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED, stream_flags,
                                        buffer_duration, 0, mix_format, nullptr);
        CoTaskMemFree(mix_format);

        if (FAILED(hr)) {
            audio_client_->Release();
            audio_client_ = nullptr;
            device->Release();
            enumerator->Release();
            return false;
        }

        hr = audio_client_->GetService(__uuidof(IAudioCaptureClient),
                                        reinterpret_cast<void**>(&capture_client_));
        if (FAILED(hr)) {
            audio_client_->Release();
            audio_client_ = nullptr;
            device->Release();
            enumerator->Release();
            return false;
        }

        capture_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        audio_client_->SetEventHandle(capture_event_);
        audio_client_->Start();

        device->Release();
        enumerator->Release();

        Logger::Info("CC: Audio device opened: " + current_device_name_ +
                     " (" + std::to_string(source_sample_rate_) + "Hz, " +
                     std::to_string(source_channels_) + "ch)");
        return true;
    }

    void ReleaseDevice() {
        if (audio_client_) {
            audio_client_->Stop();
        }
        if (capture_client_) {
            capture_client_->Release();
            capture_client_ = nullptr;
        }
        if (audio_client_) {
            audio_client_->Release();
            audio_client_ = nullptr;
        }
        if (capture_event_) {
            CloseHandle(capture_event_);
            capture_event_ = nullptr;
        }
    }

    void CaptureLoop() {
        // Resample ratio: source_sample_rate_ → 16000
        // Simple approach: accumulate samples and decimate
        double resample_ratio = 16000.0 / source_sample_rate_;
        double resample_accum = 0.0;

        while (running_) {
            DWORD wait = WaitForSingleObject(capture_event_, 100);
            if (!running_) break;
            if (wait != WAIT_OBJECT_0) continue;

            UINT32 packet_size = 0;
            capture_client_->GetNextPacketSize(&packet_size);

            while (packet_size > 0) {
                BYTE* data = nullptr;
                UINT32 num_frames = 0;
                DWORD flags = 0;
                HRESULT hr = capture_client_->GetBuffer(&data, &num_frames, &flags, nullptr, nullptr);
                if (FAILED(hr)) break;

                bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT);

                for (UINT32 i = 0; i < num_frames; i++) {
                    // Mix to mono
                    float mono = 0.0f;
                    if (!silent) {
                        const float* samples = reinterpret_cast<const float*>(data) + i * source_channels_;
                        for (int ch = 0; ch < source_channels_; ch++)
                            mono += samples[ch];
                        mono /= source_channels_;
                    }

                    // Simple resampling via accumulator
                    resample_accum += resample_ratio;
                    if (resample_accum >= 1.0) {
                        resample_accum -= 1.0;
                        buffer_.Write(&mono, 1);
                    }
                }

                capture_client_->ReleaseBuffer(num_frames);
                capture_client_->GetNextPacketSize(&packet_size);
            }
        }
    }

    std::thread capture_thread_;
    std::atomic<bool> running_{false};
    bool co_initialized_ = false;

    std::string selected_device_id_;
    std::string current_device_name_ = "None";

    IAudioClient* audio_client_ = nullptr;
    IAudioCaptureClient* capture_client_ = nullptr;
    HANDLE capture_event_ = nullptr;

    int source_sample_rate_ = 48000;
    int source_channels_ = 2;
};

std::unique_ptr<AudioCapture> AudioCapture::Create() {
    return std::make_unique<WASAPICapture>();
}

} // namespace YipOS

#endif // _WIN32
