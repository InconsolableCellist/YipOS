#pragma once

#include <string>
#include <vector>
#include <deque>
#include <cstdint>
#include <array>
#include <functional>

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
    void RenderNVRAMTab(PDAController& pda, Config& config);
    void RenderLogTab();

    void HandleKeyboardShortcuts(PDAController& pda);
    void TickSimulations(PDAController& pda);

    GLFWwindow* window_ = nullptr;

    // Log circular buffer
    std::deque<std::string> log_lines_;
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

    // OSC Query server (optional, for status display)
    OSCQueryServer* osc_query_ = nullptr;

    // When true, OSCQuery won't override the manually-set send target
    bool manual_osc_override_ = false;

    // File drop callback
    std::function<void(const std::string&)> drop_callback_;

    // Window size
    int initial_width_ = 720;
    int initial_height_ = 480;
};

} // namespace YipOS
