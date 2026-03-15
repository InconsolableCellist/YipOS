#pragma once

#include <string>
#include <vector>
#include <deque>

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

private:
    void RenderStatusTab(PDAController& pda, OSCManager& osc);
    void RenderConfigTab(PDAController& pda, Config& config);
    void RenderLogTab();

    GLFWwindow* window_ = nullptr;

    // Log circular buffer
    std::deque<std::string> log_lines_;
    static constexpr size_t MAX_LOG_LINES = 1000;
    bool log_auto_scroll_ = true;

    // Config state path (for save/load)
    std::string config_path_;
};

} // namespace YipOS
