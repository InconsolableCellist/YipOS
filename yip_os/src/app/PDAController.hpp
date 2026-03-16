#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <memory>
#include <unordered_map>

namespace YipOS {

class Config;
class PDADisplay;
class Screen;
class NetTracker;
class SystemStats;
class VRCXData;
struct VRCXWorldEntry;
struct VRCXFeedEntry;

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
};

} // namespace YipOS
