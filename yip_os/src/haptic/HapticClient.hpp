#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace YipOS {

class Config;

enum class HapticPattern {
    Notification,  // 1 short buzz (~200 ms)
    Alert,         // 2 buzzes (~150 ms each, 100 ms gap)
    Tap,           // 1 very short buzz (~50 ms)
};

// Generic SteamVR controller-vibration helper. Designed to be reused by
// FUR / DM / CHAT / VRCX notifications.
//
// Init() soft-fails (returns false, IsAvailable() == false) when SteamVR
// isn't running or when YipOS was built with YIPOS_HAS_OPENVR=OFF — the
// rest of the app stays functional.
class HapticClient {
public:
    HapticClient() = default;
    ~HapticClient();

    bool Init();
    void Shutdown();
    bool IsAvailable() const { return available_; }

    // Re-read haptics.* from Config. Safe to call any time.
    void ReloadConfig(const Config& cfg);

    // Fires asynchronously on a worker thread. `source` is a string tag
    // (e.g. "fur", "dm", "chat") used for per-source enable lookups.
    void Notify(const std::string& source, HapticPattern pattern);

    // Direct primitive — honors global enable/intensity but bypasses the
    // per-source toggle. Also async.
    void Buzz(uint32_t duration_ms, float intensity = 1.0f);

private:
    void StartWorker();
    void StopWorker();
    void WorkerLoop();
    void RescanControllers();
    void EmitPulse(uint32_t total_us, float intensity);
    void RunPattern(HapticPattern pattern);

    std::atomic<bool> available_{false};
    std::vector<uint32_t> controller_indices_;
    double last_rescan_ = 0;

    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::condition_variable cv_;
    std::mutex queue_mtx_;
    std::deque<std::function<void()>> queue_;

    // Cached config (read by call thread, no lock — atomic-ish writes are
    // not strictly safe but the data is advisory and re-read often).
    std::atomic<bool> global_enabled_{false};
    std::atomic<float> intensity_{1.0f};
    std::mutex sources_mtx_;
    std::unordered_map<std::string, bool> source_enabled_;
};

} // namespace YipOS
