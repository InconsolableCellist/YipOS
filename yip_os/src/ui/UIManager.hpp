#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <cstdint>
#include <array>
#include <functional>
#include <unordered_map>

struct GLFWwindow;

namespace YipOS {

class PDAController;
class Config;
class OSCManager;
class OSCQueryServer;

class UIManager {
public:
    UIManager();
    ~UIManager();

    bool Initialize(const std::string& title = "YipOS");
    void Shutdown();
    bool ShouldClose() const;

    void BeginFrame();
    void Render(PDAController& pda, Config& config, OSCManager& osc);
    void EndFrame();

    // Log buffer (filled by Logger callback)
    void AddLogLine(const std::string& line);

    void SetOSCQueryServer(OSCQueryServer* s) { osc_query_ = s; }
    bool IsManualOSCOverride() const { return manual_osc_override_; }
    void SetConfigPath(const std::string& path) { config_path_ = path; }
    void SetAssetsPath(const std::string& path) { assets_path_ = path; }
    void SetDropCallback(std::function<void(const std::string&)> cb) { drop_callback_ = std::move(cb); }
    void SetInitialSize(int w, int h) { initial_width_ = w; initial_height_ = h; }
    bool IsMinimized() const;
    void PollEvents();  // process window events without rendering
    void SaveWindowSize(Config& config);

private:
    void RenderStatusTab(PDAController& pda, OSCManager& osc);
    void RenderOSCTab(PDAController& pda, Config& config, OSCManager& osc);
    void RenderDisplayTab(PDAController& pda, Config& config);
    void RenderVRCXTab(PDAController& pda, Config& config);
    void RenderCCTab(PDAController& pda, Config& config);
    void RenderINTRPTab(PDAController& pda, Config& config);
    void RenderAvatarTab(PDAController& pda, Config& config);
    void RenderTextTab(PDAController& pda, Config& config, OSCManager& osc);
    void RenderStocksTab(PDAController& pda, Config& config);
    void RenderTwitchTab(PDAController& pda, Config& config);
    void RenderIMGTab(PDAController& pda, Config& config);
    void RenderDMTab(PDAController& pda, Config& config);
    void RenderShockTab(PDAController& pda, Config& config);
    void RenderNVRAMTab(PDAController& pda, Config& config);
    void RenderLogTab();

    void HandleKeyboardShortcuts(PDAController& pda);
    void TickSimulations(PDAController& pda);

    GLFWwindow* window_ = nullptr;

    // Log circular buffer (written from any thread via Logger callback)
    std::deque<std::string> log_lines_;
    std::mutex log_mutex_;
    static constexpr size_t MAX_LOG_LINES = 1000;
    bool log_auto_scroll_ = true;

    // Config state path (for save/load)
    std::string config_path_;
    std::string assets_path_;

    // VRCX config UI state
    std::array<char, 512> vrcx_path_buf_ = {};
    bool vrcx_path_initialized_ = false;

    // Avatar config UI state
    std::array<char, 512> avtr_path_buf_ = {};
    bool avtr_path_initialized_ = false;

    // Simulation state (ticked every frame, even when OSC tab hidden)
    bool sim_hr_auto_ = false;
    bool sim_bfi_auto_ = false;

    // Text tab state
    std::array<char, 1024> text_buf_ = {};
    bool text_buf_initialized_ = false;
    bool text_vrc_chatbox_ = false;

    // Stocks tab state
    std::array<char, 32> stonk_symbol_buf_ = {};

    // Twitch tab state
    std::array<char, 64> twitch_channel_buf_ = {};
    bool twitch_channel_initialized_ = false;

    // HEART custom OSC param
    std::array<char, 64> heart_custom_param_buf_ = {};
    bool heart_custom_param_initialized_ = false;

    // DM tab state
    std::array<char, 256> dm_endpoint_buf_ = {};
    bool dm_endpoint_initialized_ = false;
    std::array<char, 64> dm_name_buf_ = {};
    bool dm_name_initialized_ = false;
    std::array<char, 8> dm_join_code_buf_ = {};
    std::unordered_map<std::string, std::array<char, 256>> dm_compose_bufs_;

    // Shock tab state (OpenShock & PiShock)
    bool openshock_enabled_ = false;
    std::array<char, 128> openshock_token_buf_ = {};
    bool openshock_token_initialized_ = false;

    bool pishock_enabled_ = false;
    std::array<char, 64> pishock_username_buf_ = {};
    std::array<char, 128> pishock_apikey_buf_ = {};
    bool pishock_initialized_ = false;

    float shock_intensity_step_ = 2.5f;
    int shock_duration_step_ = 100;

    // OSC Query server (optional, for status display)
    OSCQueryServer* osc_query_ = nullptr;

    // When true, OSCQuery won't override the manually-set send target
    bool manual_osc_override_ = false;

    // File drop callback
    std::function<void(const std::string&)> drop_callback_;

    // Active tab index for two-row tab buttons
    int active_tab_ = 0;

    // Window size
    int initial_width_ = 720;
    int initial_height_ = 480;
};

} // namespace YipOS