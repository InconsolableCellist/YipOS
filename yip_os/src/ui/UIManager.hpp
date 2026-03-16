#pragma once

#include <string>
#include <vector>
#include <deque>
#include <cstdint>
#include <array>

struct GLFWwindow;

namespace YipOS {

class PDAController;
class Config;
class OSCManager;

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

    void SetConfigPath(const std::string& path) { config_path_ = path; }
    void SetAssetsPath(const std::string& path) { assets_path_ = path; }
    void SetInitialSize(int w, int h) { initial_width_ = w; initial_height_ = h; }
    void SaveWindowSize(Config& config);

private:
    void RenderStatusTab(PDAController& pda, OSCManager& osc);
    void RenderOSCTab(PDAController& pda, Config& config, OSCManager& osc);
    void RenderDisplayTab(PDAController& pda, Config& config);
    void RenderVRCXTab(PDAController& pda, Config& config);
    void RenderCCTab(PDAController& pda, Config& config);
    void RenderAvatarTab(PDAController& pda, Config& config);
    void RenderNVRAMTab(PDAController& pda, Config& config);
    void RenderLogTab();

    void HandleKeyboardShortcuts(PDAController& pda);

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

    // Window size
    int initial_width_ = 720;
    int initial_height_ = 480;
};

} // namespace YipOS
