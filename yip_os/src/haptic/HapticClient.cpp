#include "HapticClient.hpp"

#include "core/Config.hpp"
#include "core/Logger.hpp"

#include <algorithm>
#include <chrono>

#ifdef YIPOS_HAS_OPENVR
#include <openvr.h>
#endif

namespace YipOS {

namespace {
double NowSeconds() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

HapticClient::~HapticClient() {
    Shutdown();
}

bool HapticClient::Init() {
#ifdef YIPOS_HAS_OPENVR
    vr::EVRInitError err = vr::VRInitError_None;
    vr::VR_Init(&err, vr::VRApplication_Background);
    if (err != vr::VRInitError_None) {
        Logger::Info(std::string("HapticClient: SteamVR init unavailable (") +
                     vr::VR_GetVRInitErrorAsSymbol(err) + ")");
        available_ = false;
        return false;
    }
    available_ = true;
    RescanControllers();
    StartWorker();
    Logger::Info("HapticClient: SteamVR ready, " +
                 std::to_string(controller_indices_.size()) + " controller(s)");
    return true;
#else
    Logger::Debug("HapticClient: built without OpenVR support");
    available_ = false;
    return false;
#endif
}

void HapticClient::Shutdown() {
    StopWorker();
#ifdef YIPOS_HAS_OPENVR
    if (available_) {
        vr::VR_Shutdown();
        available_ = false;
    }
#endif
}

void HapticClient::ReloadConfig(const Config& cfg) {
    bool g = (cfg.GetState("haptics.enabled", "0") == "1");
    global_enabled_.store(g);

    float intensity = 1.0f;
    try { intensity = std::stof(cfg.GetState("haptics.intensity", "1.0")); }
    catch (...) {}
    intensity = std::clamp(intensity, 0.0f, 1.0f);
    intensity_.store(intensity);

    std::lock_guard<std::mutex> lk(sources_mtx_);
    source_enabled_.clear();
    // Default-on once the global toggle is on; explicit "0" overrides.
    for (const char* src : {"fur", "dm", "chat", "vrcx"}) {
        std::string key = std::string("haptics.sources.") + src;
        source_enabled_[src] = (cfg.GetState(key, "1") == "1");
    }
}

void HapticClient::Notify(const std::string& source, HapticPattern pattern) {
    if (!global_enabled_.load()) return;
    {
        std::lock_guard<std::mutex> lk(sources_mtx_);
        auto it = source_enabled_.find(source);
        if (it != source_enabled_.end() && !it->second) return;
    }
    if (!available_) return;

    std::lock_guard<std::mutex> lk(queue_mtx_);
    if (queue_.size() >= 8) return;  // drop runaway buzzes
    queue_.emplace_back([this, pattern]() { RunPattern(pattern); });
    cv_.notify_one();
}

void HapticClient::Buzz(uint32_t duration_ms, float intensity) {
    if (!global_enabled_.load() || !available_) return;
    std::lock_guard<std::mutex> lk(queue_mtx_);
    if (queue_.size() >= 8) return;
    uint32_t us = duration_ms * 1000u;
    float amp = std::clamp(intensity, 0.0f, 1.0f) * intensity_.load();
    queue_.emplace_back([this, us, amp]() { EmitPulse(us, amp); });
    cv_.notify_one();
}

void HapticClient::StartWorker() {
    stop_.store(false);
    worker_ = std::thread(&HapticClient::WorkerLoop, this);
}

void HapticClient::StopWorker() {
    if (!worker_.joinable()) return;
    stop_.store(true);
    cv_.notify_all();
    worker_.join();
}

void HapticClient::WorkerLoop() {
    while (!stop_.load()) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(queue_mtx_);
            cv_.wait(lk, [&] { return stop_.load() || !queue_.empty(); });
            if (stop_.load()) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        if (job) job();
    }
}

void HapticClient::RescanControllers() {
#ifdef YIPOS_HAS_OPENVR
    controller_indices_.clear();
    if (!vr::VRSystem()) return;
    for (uint32_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++) {
        if (vr::VRSystem()->GetTrackedDeviceClass(i) ==
            vr::TrackedDeviceClass_Controller) {
            controller_indices_.push_back(i);
        }
    }
    last_rescan_ = NowSeconds();
#endif
}

void HapticClient::EmitPulse(uint32_t total_us, float intensity) {
#ifdef YIPOS_HAS_OPENVR
    if (!vr::VRSystem()) return;
    if (NowSeconds() - last_rescan_ > 5.0) RescanControllers();
    if (controller_indices_.empty()) return;

    // OpenVR's legacy TriggerHapticPulse is capped at ~4000 µs per call, with
    // no amplitude param. We approximate amplitude by scaling per-pulse
    // duration, then loop with short gaps to compose pulses longer than 4 ms.
    constexpr uint32_t kMaxPulseUs = 4000;
    constexpr uint32_t kGapUs = 4000;
    uint32_t pulse_us = static_cast<uint32_t>(
        kMaxPulseUs * std::clamp(intensity, 0.0f, 1.0f));
    if (pulse_us < 200) pulse_us = 200;

    uint32_t elapsed = 0;
    while (elapsed < total_us && !stop_.load()) {
        for (auto idx : controller_indices_) {
            vr::VRSystem()->TriggerHapticPulse(idx, 0, pulse_us);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(pulse_us + kGapUs));
        elapsed += pulse_us + kGapUs;
    }
#else
    (void)total_us; (void)intensity;
#endif
}

void HapticClient::RunPattern(HapticPattern pattern) {
    float amp = intensity_.load();
    switch (pattern) {
        case HapticPattern::Tap:
            EmitPulse(50'000, amp);
            break;
        case HapticPattern::Notification:
            EmitPulse(200'000, amp);
            break;
        case HapticPattern::Alert:
            EmitPulse(150'000, amp);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!stop_.load()) EmitPulse(150'000, amp);
            break;
    }
}

} // namespace YipOS
