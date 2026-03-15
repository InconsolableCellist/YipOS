#include "UIManager.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "app/ScreenBuffer.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "core/Glyphs.hpp"
#include "net/OSCManager.hpp"
#include "screens/Screen.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <cstdio>
#include <chrono>

namespace YipOS {

UIManager::UIManager() = default;

UIManager::~UIManager() {
    Shutdown();
}

bool UIManager::Initialize(const std::string& title) {
    if (!glfwInit()) {
        Logger::Error("Failed to initialize GLFW");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    window_ = glfwCreateWindow(720, 480, title.c_str(), nullptr, nullptr);
    if (!window_) {
        Logger::Error("Failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // vsync

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        Logger::Error("Failed to initialize GLAD");
        return false;
    }

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    Logger::Info("UI initialized: " + title);
    return true;
}

void UIManager::Shutdown() {
    if (!window_) return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window_);
    glfwTerminate();
    window_ = nullptr;
    Logger::Info("UI shutdown");
}

bool UIManager::ShouldClose() const {
    return window_ && glfwWindowShouldClose(window_);
}

void UIManager::BeginFrame() {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void UIManager::Render(PDAController& pda, Config& config, OSCManager& osc) {
    // Fill the entire GLFW window, no move/resize/collapse
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("YipOS Control Panel", nullptr, flags);

    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Status")) {
            RenderStatusTab(pda, osc);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Config")) {
            RenderConfigTab(pda, config);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Log")) {
            RenderLogTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void UIManager::EndFrame() {
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window_, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
}

void UIManager::AddLogLine(const std::string& line) {
    if (log_lines_.size() >= MAX_LOG_LINES) {
        log_lines_.pop_front();
    }
    log_lines_.push_back(line);
}

// --- Tab Implementations ---

void UIManager::RenderStatusTab(PDAController& pda, OSCManager& osc) {
    if (pda.IsBooting()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "BOOTING...");
        return;
    }

    Screen* current = pda.GetCurrentScreen();
    std::string screen_name = current ? current->name : "NONE";

    ImGui::Text("Current Screen: %s", screen_name.c_str());
    ImGui::Text("Stack Depth: %d", pda.GetScreenStackDepth());
    ImGui::Text("Spinner: %c", pda.GetSpinnerChar());

    int remaining = pda.GetDisplay().BufferedRemaining();
    if (remaining > 0) {
        ImGui::Text("Buffered Writes: %d", remaining);
    } else {
        ImGui::Text("Buffered Writes: idle");
    }

    std::string last = pda.GetLastInput();
    if (!last.empty()) {
        ImGui::Text("Last Input: %s", last.c_str());
    }

    ImGui::Separator();

    // Screen buffer dump
    if (ImGui::CollapsingHeader("Screen Buffer", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string dump = pda.GetDisplay().GetScreen().Dump();
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // monospace
        ImGui::TextUnformatted(dump.c_str());
        ImGui::PopFont();
    }

    // Recent OSC receives
    if (ImGui::CollapsingHeader("OSC Incoming")) {
        auto recvs = osc.GetRecentRecvs();
        for (auto it = recvs.rbegin(); it != recvs.rend() && std::distance(recvs.rbegin(), it) < 20; ++it) {
            ImGui::Text("  %s = %.2f", it->path.c_str(), it->value);
        }
    }
}

void UIManager::RenderConfigTab(PDAController& pda, Config& config) {
    static char ip_buf[64] = {};
    if (ip_buf[0] == 0) {
        std::snprintf(ip_buf, sizeof(ip_buf), "%s", config.osc_ip.c_str());
    }

    ImGui::InputText("OSC IP", ip_buf, sizeof(ip_buf));
    ImGui::InputInt("Send Port", &config.osc_send_port);
    ImGui::InputInt("Listen Port", &config.osc_listen_port);

    ImGui::Separator();
    ImGui::Text("Display Calibration");
    ImGui::SliderFloat("Y Offset", &config.y_offset, -0.5f, 0.5f);
    ImGui::SliderFloat("Y Scale", &config.y_scale, 0.1f, 2.0f);
    ImGui::SliderFloat("Y Curve", &config.y_curve, 0.1f, 3.0f);

    ImGui::Separator();
    ImGui::Text("Timing");
    ImGui::SliderFloat("Write Delay", &config.write_delay, 0.01f, 0.2f, "%.3f s");
    ImGui::SliderFloat("Settle Delay", &config.settle_delay, 0.01f, 0.1f, "%.3f s");
    ImGui::SliderFloat("Refresh Interval", &config.refresh_interval, 0.0f, 30.0f, "%.1f s");

    ImGui::Separator();
    static const char* levels[] = {"DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"};
    static int current_level = 1;
    if (ImGui::Combo("Log Level", &current_level, levels, 5)) {
        config.log_level = levels[current_level];
        Logger::SetLogLevel(Logger::StringToLevel(config.log_level));
    }

    ImGui::Separator();
    ImGui::Text("Startup");
    ImGui::Checkbox("Boot Animation", &config.boot_animation);

    ImGui::Separator();
    if (ImGui::Button("Save Config")) {
        config.osc_ip = ip_buf;
        if (!config_path_.empty()) {
            config.SaveToFile(config_path_);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Defaults")) {
        config = Config{};
        std::snprintf(ip_buf, sizeof(ip_buf), "%s", config.osc_ip.c_str());
    }
    ImGui::SameLine();
    if (!pda.IsBooting()) {
        if (ImGui::Button("Reboot PDA")) {
            pda.Reboot();
        }
    } else {
        ImGui::TextDisabled("Reboot PDA");
    }
}

void UIManager::RenderLogTab() {
    ImGui::Checkbox("Auto-scroll", &log_auto_scroll_);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        log_lines_.clear();
    }

    ImGui::Separator();
    ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& line : log_lines_) {
        ImGui::TextUnformatted(line.c_str());
    }
    if (log_auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

} // namespace YipOS
