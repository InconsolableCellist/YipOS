#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <array>
#include <atomic>

namespace YipOS {

class Config;
class PDADisplay;
class Screen;
class NetTracker;
class SystemStats;
class VRCXData;
struct VRCXWorldEntry;
struct VRCXFeedEntry;
class WhisperWorker;
class AudioCapture;
class VRCAvatarData;
struct VRCAvatarEntry;
class OSCManager;

class PDAController {
public:
    PDAController(PDADisplay& display, NetTracker& net_tracker, Config& config, float refresh_interval = 0);
    ~PDAController();

    // Main loop methods
    void ProcessInput();
    bool TickRefresh();
    void MaybeUpdate();
    void MaybeRefresh();
    void UpdateClock();
    void ToggleCursor();

    // Screen management
    void PushScreen(const std::string& screen_name);
    void PopScreen();
    void GoHome();
    void StartRender(Screen* screen);

    // Input (called from OSC recv thread)
    void OnCRTInput(const std::string& address, float value);
    void QueueInput(const std::string& param_suffix);

    // Pending navigation (set by HomeScreen)
    void SetPendingNavigate(const std::string& label);

    // Boot sequence
    void RunBootSequence();
    void Reboot();
    bool IsBooting() const { return booting_; }
    bool ConsumeRebootRequest() { bool r = reboot_requested_; reboot_requested_ = false; return r; }

    // Accessors
    bool IsBufferActive() const;
    PDADisplay& GetDisplay() { return display_; }
    NetTracker& GetNetTracker() { return net_tracker_; }
    SystemStats& GetSystemStats() { return *system_stats_; }
    Config& GetConfig() { return config_; }
    VRCXData* GetVRCXData() { return vrcx_data_; }
    void SetVRCXData(VRCXData* d) { vrcx_data_ = d; }
    void SetSelectedWorld(const VRCXWorldEntry* w) { selected_world_ = w; }
    const VRCXWorldEntry* GetSelectedWorld() const { return selected_world_; }
    void SetSelectedFeed(const VRCXFeedEntry* f) { selected_feed_ = f; }
    const VRCXFeedEntry* GetSelectedFeed() const { return selected_feed_; }
    WhisperWorker* GetWhisperWorker() { return whisper_worker_; }
    void SetWhisperWorker(WhisperWorker* w) { whisper_worker_ = w; }
    AudioCapture* GetAudioCapture() { return audio_capture_; }
    void SetAudioCapture(AudioCapture* a) { audio_capture_ = a; }
    VRCAvatarData* GetAvatarData() { return avatar_data_; }
    void SetAvatarData(VRCAvatarData* d) { avatar_data_ = d; }
    void SetSelectedAvatar(const VRCAvatarEntry* a) { selected_avatar_ = a; }
    const VRCAvatarEntry* GetSelectedAvatar() const { return selected_avatar_; }
    OSCManager* GetOSCManager() { return osc_; }
    void SetOSCManager(OSCManager* o) { osc_ = o; }

    // Hard lock (full LOCK screen from home tile)
    void SetLocked(bool locked);
    bool IsLocked() const { return locked_; }

    // Soft lock (autolock — overlay on current screen)
    bool IsSoftLocked() const { return soft_locked_; }
    bool IsLockFlashing() const;
    void ResetAutolockTimer();

    // Notification seen tracking
    bool HasUnseenNotifs();
    bool HasUnseenNotifsCached() const { return has_unseen_notifs_; }
    void RefreshNotifCache();
    void MarkNotifsSeen();
    void ClearAllNotifs();

    // SPVR device status (updated from OSC handler, read by StayScreen)
    // Indices: 0=HMD, 1=ControllerLeft, 2=ControllerRight, 3=FootLeft, 4=FootRight, 5=Hip
    static constexpr int SPVR_DEVICE_COUNT = 6;
    static constexpr const char* SPVR_DEVICE_NAMES[SPVR_DEVICE_COUNT] = {
        "HMD", "ControllerLeft", "ControllerRight", "FootLeft", "FootRight", "Hip"
    };
    // Status values: 0=disabled, 1=unlocked, 2=locked, 3=warning, 4=disobey, 5=OOB
    void SetSPVRStatus(int device_index, int status);
    int GetSPVRStatus(int device_index) const;

    Screen* GetCurrentScreen() const;
    int GetScreenStackDepth() const { return static_cast<int>(screen_stack_.size()); }
    char GetSpinnerChar() const;
    std::string GetLastInput() const;
    double GetLastInputTime() const { return last_input_time_; }

    static constexpr float NAVIGATE_DELAY = 0.3f;
    static constexpr const char* SPINNER = "|/-\\";

private:
    void ResetRefresh();
    float GetRefreshInterval() const;

    PDADisplay& display_;
    NetTracker& net_tracker_;
    Config& config_;
    std::unique_ptr<SystemStats> system_stats_;

    // Input queue (thread-safe: OSC recv thread → main thread)
    std::queue<std::string> input_queue_;
    std::mutex input_mutex_;

    // Debounce
    std::unordered_map<std::string, double> last_trigger_;
    static constexpr double DEBOUNCE_MS = 300.0;

    // Screen stack
    std::vector<std::unique_ptr<Screen>> screen_stack_;

    // State
    int cursor_frame_ = 0;
    float refresh_interval_;
    double last_refresh_ = 0;
    double last_update_ = 0;
    std::string pending_navigate_;
    double navigate_time_ = 0;
    std::string last_input_;
    double last_input_time_ = 0;
    bool booting_ = false;
    bool reboot_requested_ = false;
    VRCXData* vrcx_data_ = nullptr;
    const VRCXWorldEntry* selected_world_ = nullptr;
    const VRCXFeedEntry* selected_feed_ = nullptr;
    WhisperWorker* whisper_worker_ = nullptr;
    AudioCapture* audio_capture_ = nullptr;
    VRCAvatarData* avatar_data_ = nullptr;
    const VRCAvatarEntry* selected_avatar_ = nullptr;
    OSCManager* osc_ = nullptr;

    // Heart rate (updated from OSC recv thread)
    std::atomic<int> hr_bpm_{0};
    std::atomic<double> hr_last_update_{0};
public:
    void SetHeartRate(int bpm);
    int GetHeartRate() const { return hr_bpm_.load(); }
    bool HasHeartRate() const;
    static constexpr double HR_TIMEOUT = 10.0;  // consider HR stale after 10s

    // BFI (BrainFlowsIntoVRChat) params (updated from OSC recv thread)
    // Each entry: { "OSC/sub/path" (after "BFI/"), "DisplayName", is_positive_only }
    struct BFIParamDef {
        const char* osc_suffix;   // matched against address after "BFI/"
        const char* display_name; // shown on screen (max ~14 chars)
        bool positive_only;       // true = [0,1], false = [-1,1]
    };
    static constexpr int BFI_PARAM_COUNT = 26;
    static constexpr BFIParamDef BFI_PARAMS[BFI_PARAM_COUNT] = {
        // NeuroFB — Focus
        {"NeuroFB/FocusAvg",       "FocusAvg",      false},
        {"NeuroFB/FocusPosAvg",    "FocusPosAvg",   true},
        {"NeuroFB/FocusLeft",      "FocusLeft",     false},
        {"NeuroFB/FocusPosLeft",   "FocusPosLeft",  true},
        {"NeuroFB/FocusRight",     "FocusRight",    false},
        {"NeuroFB/FocusPosRight",  "FocusPosRight", true},
        // NeuroFB — Relax
        {"NeuroFB/RelaxAvg",       "RelaxAvg",      false},
        {"NeuroFB/RelaxPosAvg",    "RelaxPosAvg",   true},
        {"NeuroFB/RelaxLeft",      "RelaxLeft",     false},
        {"NeuroFB/RelaxPosLeft",   "RelaxPosLeft",  true},
        {"NeuroFB/RelaxRight",     "RelaxRight",    false},
        {"NeuroFB/RelaxPosRight",  "RelaxPosRight", true},
        // PwrBands — Avg
        {"PwrBands/Avg/Delta",     "PB Delta",      true},
        {"PwrBands/Avg/Theta",     "PB Theta",      true},
        {"PwrBands/Avg/Alpha",     "PB Alpha",      true},
        {"PwrBands/Avg/Beta",      "PB Beta",       true},
        {"PwrBands/Avg/Gamma",     "PB Gamma",      true},
        // Biometrics
        {"Biometrics/OxygenPercent",     "O2 %",      true},
        {"Biometrics/HeartBeatsPerSecond","Heart BPS", true},
        {"Biometrics/BreathsPerSecond",  "Breath BPS",true},
        // Addons
        {"Addons/HueShift",        "HueShift",      true},
        // Info
        {"Info/BatteryLevel",      "Battery",       true},
        {"Info/SecondsSinceLastUpdate", "LastUpdate", true},
        // PwrBands — Left
        {"PwrBands/Left/Alpha",    "PB L Alpha",    true},
        // PwrBands — Right
        {"PwrBands/Right/Alpha",   "PB R Alpha",    true},
        // MLAction
        {"MLAction/ActionH",       "ML ActionH",    false},
    };
    void SetBFIParam(int index, float value);
    float GetBFIParam(int index) const;
    bool HasBFIData() const;
    static constexpr double BFI_TIMEOUT = 10.0;

private:
    std::array<std::atomic<float>, BFI_PARAM_COUNT> bfi_values_{};
    std::atomic<double> bfi_last_update_{0};

    // Hard lock state (LOCK screen)
    bool locked_ = false;

    // Soft lock state (autolock)
    bool soft_locked_ = false;
    int soft_lock_sel_count_ = 0;
    double soft_lock_last_sel_ = 0;
    double lock_flash_until_ = 0;
    static constexpr double SOFT_LOCK_SEL_WINDOW = 2.0;
    static constexpr int SOFT_LOCK_SEL_NEEDED = 3;
    static constexpr double LOCK_FLASH_DURATION = 0.5;

    // Activity tracking for autolock
    double last_activity_ = 0;

    // Notification cache
    bool has_unseen_notifs_ = false;
    double last_notif_check_ = 0;
    static constexpr double NOTIF_CHECK_INTERVAL = 30.0;

    // SPVR device status (thread-safe: updated from OSC recv thread)
    std::array<std::atomic<int>, SPVR_DEVICE_COUNT> spvr_status_{};
};

} // namespace YipOS
