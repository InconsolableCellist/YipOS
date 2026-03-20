#include "UIManager.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "app/ScreenBuffer.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "core/Glyphs.hpp"
#include "net/OSCManager.hpp"
#include "net/OSCQueryServer.hpp"
#include "net/VRCXData.hpp"
#include "net/VRCAvatarData.hpp"
#include "net/StockClient.hpp"
#include "audio/AudioCapture.hpp"
#include "audio/WhisperWorker.hpp"
#include "screens/Screen.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>
#include <cmath>
#include <chrono>
#include <algorithm>

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

    window_ = glfwCreateWindow(initial_width_, initial_height_, title.c_str(), nullptr, nullptr);
    if (!window_) {
        Logger::Error("Failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // vsync

    // File drop callback
    glfwSetWindowUserPointer(window_, this);
    glfwSetDropCallback(window_, [](GLFWwindow* w, int count, const char** paths) {
        auto* self = static_cast<UIManager*>(glfwGetWindowUserPointer(w));
        if (self && self->drop_callback_ && count > 0) {
            self->drop_callback_(paths[0]);
        }
    });

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

void UIManager::SaveWindowSize(Config& config) {
    if (!window_) return;
    int w, h;
    glfwGetWindowSize(window_, &w, &h);
    config.SetState("ui.width", std::to_string(w));
    config.SetState("ui.height", std::to_string(h));
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

void UIManager::HandleKeyboardShortcuts(PDAController& pda) {
    // Skip if any text input is active (typing in a field)
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;

    // 1-5: touch row 1, QWERT: touch row 2, ASDFG: touch row 3
    ImGuiKey row1keys[] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5};
    ImGuiKey row2keys[] = {ImGuiKey_Q, ImGuiKey_W, ImGuiKey_E, ImGuiKey_R, ImGuiKey_T};
    ImGuiKey row3keys[] = {ImGuiKey_A, ImGuiKey_S, ImGuiKey_D, ImGuiKey_F, ImGuiKey_G};
    for (int i = 0; i < 5; i++) {
        if (ImGui::IsKeyPressed(row1keys[i], false)) {
            pda.QueueInput(std::to_string(i + 1) + "1");
            return;
        }
        if (ImGui::IsKeyPressed(row2keys[i], false)) {
            pda.QueueInput(std::to_string(i + 1) + "2");
            return;
        }
        if (ImGui::IsKeyPressed(row3keys[i], false)) {
            pda.QueueInput(std::to_string(i + 1) + "3");
            return;
        }
    }

    // F1-F5: physical buttons
    // F1=TL, F2=ML, F3=BL, F4=TR, F5=Joystick
    static const char* fkeys[] = {"TL", "ML", "BL", "TR", "Joystick"};
    ImGuiKey fkeyIds[] = {ImGuiKey_F1, ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4, ImGuiKey_F5};
    for (int i = 0; i < 5; i++) {
        if (ImGui::IsKeyPressed(fkeyIds[i], false)) {
            pda.QueueInput(fkeys[i]);
            return;
        }
    }
}

void UIManager::Render(PDAController& pda, Config& config, OSCManager& osc) {
    HandleKeyboardShortcuts(pda);

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
        if (ImGui::BeginTabItem("OSC")) {
            RenderOSCTab(pda, config, osc);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Display")) {
            RenderDisplayTab(pda, config);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("VRCX")) {
            RenderVRCXTab(pda, config);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("CC")) {
            RenderCCTab(pda, config);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Avatar")) {
            RenderAvatarTab(pda, config);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Text")) {
            RenderTextTab(pda, config, osc);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Stocks")) {
            RenderStocksTab(pda, config);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("NVRAM")) {
            RenderNVRAMTab(pda, config);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Log")) {
            RenderLogTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();

    // Run auto-sim ticks outside of tab rendering so they keep running
    // even when the OSC tab isn't visible
    TickSimulations(pda);
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
    auto& display = pda.GetDisplay();

    // --- Header ---
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "YIP OS v1.0");
    ImGui::SameLine();
    ImGui::TextDisabled("(C) Foxipso 2026");
    ImGui::TextDisabled("Enable the PDA and Williams Tube in VRChat to see the output!");
    ImGui::Text("OS: Up and running");
    ImGui::Separator();

    ImGui::Dummy(ImVec2(0.0f, 20.0f));
    // --- Input Controls ---
    ImVec2 nav_btn(60, 28);
    ImVec2 touch_btn(40, 28);

    // Row 1: BACK | touch row 1 | SEL
    if (ImGui::Button("BACK", nav_btn)) pda.QueueInput("TL");
    ImGui::SameLine(0, 8);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0, 8);
    for (int c = 1; c <= 5; c++) {
        char label[4];
        std::snprintf(label, sizeof(label), "%d1", c);
        if (ImGui::Button(label, touch_btn)) pda.QueueInput(label);
        if (c < 5) ImGui::SameLine(0, 4);
    }
    ImGui::SameLine(0, 8);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0, 8);
    if (ImGui::Button("SEL", nav_btn)) pda.QueueInput("TR");

    ImGui::Dummy(ImVec2(0.0f, 20.0f));

    // Row 2: PG UP | touch row 2
    if (ImGui::Button("PG UP", nav_btn)) pda.QueueInput("ML");
    ImGui::SameLine(0, 8);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0, 8);
    for (int c = 1; c <= 5; c++) {
        char label[4];
        std::snprintf(label, sizeof(label), "%d2", c);
        if (ImGui::Button(label, touch_btn)) pda.QueueInput(label);
        if (c < 5) ImGui::SameLine(0, 4);
    }

    // Row 3: PG DWN | touch row 3 | JOY DWN
    if (ImGui::Button("PG DWN", nav_btn)) pda.QueueInput("BL");
    ImGui::SameLine(0, 8);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0, 8);
    for (int c = 1; c <= 5; c++) {
        char label[4];
        std::snprintf(label, sizeof(label), "%d3", c);
        if (ImGui::Button(label, touch_btn)) pda.QueueInput(label);
        if (c < 5) ImGui::SameLine(0, 4);
    }
    ImGui::SameLine(0, 8);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0, 8);
    if (ImGui::Button("JOY\nDWN", nav_btn)) pda.QueueInput("Joystick");

    ImGui::Dummy(ImVec2(0.0f, 20.0f));

    ImGui::Separator();

    // --- System State ---
    Screen* current = pda.GetCurrentScreen();
    std::string screen_name = current ? current->name : "NONE";

    // OSC connection
    if (osc.IsRunning()) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "OSC: connected");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "OSC: disconnected");
    }
    ImGui::SameLine(200);
    if (pda.IsBooting()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "State: BOOTING");
    } else {
        ImGui::Text("Screen: %s (%d)", screen_name.c_str(), pda.GetScreenStackDepth());
    }
    ImGui::SameLine(400);
    ImGui::Text("Total writes: %d", display.GetTotalWrites());

    // Write head state
    {
        const char* mode_str = "TEXT";
        if (display.GetMode() == PDADisplay::MODE_MACRO) mode_str = "MACRO";
        else if (display.GetMode() == PDADisplay::MODE_CLEAR) mode_str = "CLEAR";

        int writes = display.BufferedRemaining();
        int last_char = display.GetLastCharIdx();
        char ch = (last_char >= 32 && last_char <= 126) ? static_cast<char>(last_char) : '?';

        ImGui::Text("Write head: X=%.3f Y=%.3f  Mode=%s  Char=%d ('%c')",
                     display.GetHWCursorX(), display.GetHWCursorY(),
                     mode_str, last_char, ch);
        ImGui::SameLine(400);
        if (writes > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Queued: %d", writes);
        } else {
            ImGui::TextDisabled("Queued: idle");
        }
    }

    // Last input
    std::string last_input = pda.GetLastInput();
    if (!last_input.empty()) {
        double age = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count() - pda.GetLastInputTime();
        if (age < 10.0) {
            ImGui::Text("Last input: %s (%.1fs ago)", last_input.c_str(), age);
        }
    }

    // Subsystem status line
    {
        auto* whisper = pda.GetWhisperWorker();
        bool any_locked = false;
        for (int d = 0; d < PDAController::SPVR_DEVICE_COUNT; d++) {
            if (pda.GetSPVRStatus(d) >= 2) { any_locked = true; break; }
        }

        if ((whisper && whisper->IsRunning()) || any_locked) {
            if (whisper && whisper->IsRunning()) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "CC: listening");
                std::string latest = whisper->PeekLatest();
                if (!latest.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("| %s", latest.c_str());
                }
            }
            if (any_locked) {
                if (whisper && whisper->IsRunning()) ImGui::SameLine(400);
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "SPVR: locked");
            }
        }
    }

    ImGui::Separator();

    // --- Keyboard shortcuts ---
    ImGui::TextDisabled("Keys: 1-5 row1 | Q-T row2 | A-G row3 | F1=HOME F2=PGUP F3=PGDN F4=SEL F5=JOY");

    ImGui::Separator();

    // --- Recent OSC activity ---
    ImGui::Text("Recent OSC");
    float footer_h = ImGui::GetFrameHeightWithSpacing() + 4;
    ImGui::BeginChild("OSCActivity", ImVec2(0, -footer_h), true);

    ImGui::TextDisabled("Sent (last 10):");
    auto sends = osc.GetRecentSends();
    int send_count = 0;
    for (auto it = sends.rbegin(); it != sends.rend() && send_count < 10; ++it, ++send_count) {
        ImGui::Text("  > %s = %.2f", it->path.c_str(), it->value);
    }
    if (sends.empty()) ImGui::TextDisabled("  (none)");

    ImGui::Spacing();
    ImGui::TextDisabled("Received (last 10):");
    auto recvs = osc.GetRecentRecvs();
    int recv_count = 0;
    for (auto it = recvs.rbegin(); it != recvs.rend() && recv_count < 10; ++it, ++recv_count) {
        ImGui::Text("  < %s = %.2f", it->path.c_str(), it->value);
    }
    if (recvs.empty()) ImGui::TextDisabled("  (none)");

    ImGui::EndChild();

    // --- Footer ---
    ImGui::TextDisabled("foxipso.com");
}

// --- OSC Tab ---
void UIManager::RenderOSCTab(PDAController& pda, Config& config, OSCManager& osc) {
    ImGui::Text("OSC Configuration");
    ImGui::TextDisabled("YipOS uses OSC Query for automatic VRChat service discovery.");

    ImGui::Separator();

    // --- OSC Query Status ---
    if (osc_query_) {
        ImGui::Text("OSC Query");
        if (osc_query_->IsVRChatConnected()) {
            auto port = osc_query_->GetVRChatOSCPort();
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f),
                "VRChat discovered (OSC port %d)", port ? *port : 0);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                "Searching for VRChat...");
        }
        ImGui::TextDisabled("HTTP port: %d  |  OSC listen port: %d",
            osc_query_->GetHTTPPort(), osc_query_->GetOSCPort());
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
            "Note: Close Unity if open -- its OSC listener can block VRChat discovery.");
    } else if (config.osc_query_enabled) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
            "OSC Query failed to start — using static ports");
    } else {
        ImGui::TextDisabled("OSC Query is disabled in config.ini");
    }

    ImGui::Separator();

    if (osc.IsRunning()) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "OSC Status: Running");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "OSC Status: Disconnected");
    }

    ImGui::Separator();

    // --- Static port config (collapsible fallback) ---
    if (ImGui::CollapsingHeader("Manual Port Configuration")) {
        ImGui::TextDisabled("Only needed when OSC Query is disabled or unavailable.");
        ImGui::Spacing();

        static char ip_buf[64] = {};
        if (ip_buf[0] == 0) {
            std::snprintf(ip_buf, sizeof(ip_buf), "%s", config.osc_ip.c_str());
        }

        ImGui::InputText("IP Address", ip_buf, sizeof(ip_buf));
        ImGui::InputInt("Send Port", &config.osc_send_port);
        ImGui::TextDisabled("Port to send OSC messages to (VRChat default: 9000)");
        ImGui::InputInt("Listen Port", &config.osc_listen_port);
        ImGui::TextDisabled("Port to receive OSC messages on (requires restart)");

        ImGui::Spacing();
        if (ImGui::Button("Save")) {
            config.osc_ip = ip_buf;
            if (!config_path_.empty()) config.SaveToFile(config_path_);
        }
    }

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Simulate OSC Input");
    ImGui::TextDisabled("Inject values as if received from external apps (heart rate, BFI, etc.).");

    // --- Heart Rate ---
    ImGui::Spacing();
    ImGui::Text("Heart Rate");
    static int sim_hr_bpm = 75;
    ImGui::SliderInt("BPM", &sim_hr_bpm, 40, 200);
    ImGui::SameLine();
    if (ImGui::Button("Send HR")) {
        pda.SetHeartRate(sim_hr_bpm);
    }
    ImGui::Checkbox("Auto (1 Hz)", &sim_hr_auto_);
    ImGui::SameLine();
    ImGui::TextDisabled("Sends current slider value every second");
    // Auto HR tick runs in TickSimulations() so it works when tab is hidden

    // --- BFI ---
    ImGui::Spacing();
    ImGui::Text("BFI (BrainFlowsIntoVRChat) — %d params", PDAController::BFI_PARAM_COUNT);
    ImGui::Checkbox("Auto BFI (1 Hz)", &sim_bfi_auto_);
    ImGui::SameLine();
    ImGui::TextDisabled("Sends sine waves to all %d params", PDAController::BFI_PARAM_COUNT);
    // Auto BFI tick runs in TickSimulations()
}

// --- Display Tab ---
void UIManager::RenderDisplayTab(PDAController& pda, Config& config) {
    ImGui::Text("Display & Timing");
    ImGui::TextDisabled("Calibrate the Williams Tube write head position and timing.");

    ImGui::Separator();

    ImGui::Text("Y Calibration");
    ImGui::TextDisabled("Adjusts vertical positioning of text on the CRT display.");
    ImGui::SliderFloat("Y Offset", &config.y_offset, -0.5f, 0.5f);
    ImGui::SliderFloat("Y Scale", &config.y_scale, 0.1f, 2.0f);
    ImGui::SliderFloat("Y Curve", &config.y_curve, 0.1f, 3.0f);

    ImGui::Separator();

    ImGui::Text("Write Timing");
    ImGui::TextDisabled("Controls how fast characters are written to the display.");
    ImGui::SliderFloat("Write Delay", &config.write_delay, 0.01f, 0.2f, "%.3f s");
    ImGui::SliderFloat("Settle Delay", &config.settle_delay, 0.01f, 0.1f, "%.3f s");
    ImGui::SliderFloat("Refresh Interval", &config.refresh_interval, 0.0f, 30.0f, "%.1f s");
    ImGui::TextDisabled("How often the full screen is re-rendered (0 = never).");

    ImGui::Separator();

    static const char* levels[] = {"DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"};
    static int current_level = 1;
    if (ImGui::Combo("Log Level", &current_level, levels, 5)) {
        config.log_level = levels[current_level];
        Logger::SetLogLevel(Logger::StringToLevel(config.log_level));
    }

    ImGui::Separator();

    if (ImGui::Button("Save")) {
        if (!config_path_.empty()) config.SaveToFile(config_path_);
    }
    ImGui::SameLine();
    if (!pda.IsBooting()) {
        if (ImGui::Button("Reboot PDA")) {
            pda.Reboot();
        }
    }
}

// --- VRCX Tab ---
void UIManager::RenderVRCXTab(PDAController& pda, Config& config) {
    ImGui::Text("VRCX Integration");
    ImGui::TextDisabled("Reads world history and feed from VRCX's local SQLite database.");
    ImGui::TextDisabled("Requires VRCX to be installed. Data is read-only.");

    ImGui::Separator();

    if (ImGui::Checkbox("Enable VRCX", &config.vrcx_enabled)) {
        if (!config_path_.empty()) config.SaveToFile(config_path_);
    }

    if (config.vrcx_enabled) {
        if (!vrcx_path_initialized_) {
            std::string initial = config.vrcx_db_path.empty()
                ? VRCXData::DefaultDBPath() : config.vrcx_db_path;
            std::snprintf(vrcx_path_buf_.data(), vrcx_path_buf_.size(), "%s", initial.c_str());
            vrcx_path_initialized_ = true;
        }

        ImGui::InputText("DB Path", vrcx_path_buf_.data(), vrcx_path_buf_.size());
#ifdef _WIN32
        ImGui::TextDisabled("Default: %%APPDATA%%\\VRCX\\VRCX.sqlite3");
#else
        ImGui::TextDisabled("Default: ~/.config/VRCX/VRCX.sqlite3");
#endif

        VRCXData* vrcx = pda.GetVRCXData();
        if (vrcx && vrcx->IsOpen()) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Connected");
            ImGui::SameLine();
            ImGui::Text("(%d worlds)", vrcx->GetWorldCount());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Not connected");
        }

        if (ImGui::Button("Connect")) {
            std::string path(vrcx_path_buf_.data());
            config.vrcx_db_path = path;
            if (vrcx) {
                vrcx->Close();
                if (vrcx->Open(path)) {
                    Logger::Info("VRCX reconnected: " + path);
                } else {
                    Logger::Warning("VRCX connect failed: " + path);
                }
            }
            if (!config_path_.empty()) config.SaveToFile(config_path_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Disconnect")) {
            if (vrcx) vrcx->Close();
        }
    } else {
        vrcx_path_initialized_ = false;
        VRCXData* vrcx = pda.GetVRCXData();
        if (vrcx && vrcx->IsOpen()) vrcx->Close();
    }
}

// --- CC Tab ---
void UIManager::RenderCCTab(PDAController& pda, Config& config) {
    ImGui::Text("Closed Captions (CC)");
    ImGui::TextDisabled("Live speech-to-text using whisper.cpp. Transcription starts");
    ImGui::TextDisabled("automatically when you navigate to the CC screen in-game.");
    ImGui::TextDisabled("Uses Vulkan GPU acceleration (NVIDIA/AMD) with CPU fallback.");
    ImGui::TextDisabled("All output is translated to English (ASCII character set only).");

    ImGui::Separator();

    auto* whisper = pda.GetWhisperWorker();
    auto* audio = pda.GetAudioCapture();
    if (!whisper || !audio) return;

    // Model
    ImGui::Text("Model");
    if (whisper->IsModelLoaded()) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Loaded: %s", whisper->GetModelName().c_str());
    } else {
        std::string saved = config.GetState("cc.model");
        if (!saved.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Saved: %s (not loaded yet)", saved.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "No model configured");
        }
    }

    {
        static std::vector<std::string> available_models;
        static bool models_scanned = false;
        if (!models_scanned) {
            available_models = WhisperWorker::ScanAvailableModels();
            models_scanned = true;
        }

        if (available_models.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "No models found");
        } else {
            std::string preview = whisper->IsModelLoaded()
                ? whisper->GetModelName() : "(select model)";
            if (ImGui::BeginCombo("##cc_model", preview.c_str())) {
                for (auto& m : available_models) {
                    bool is_selected = (whisper->IsModelLoaded() && whisper->GetModelName() == m);
                    if (ImGui::Selectable(m.c_str(), is_selected)) {
                        if (whisper->LoadModel(WhisperWorker::DefaultModelPath(m)))
                            config.SetState("cc.model", whisper->GetModelName());
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button("Rescan Models")) {
            available_models = WhisperWorker::ScanAvailableModels();
        }
        ImGui::TextDisabled("Place ggml-*.bin files in your models folder.");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
#ifdef _WIN32
            ImGui::Text("%%APPDATA%%\\yip_os\\models\\");
            ImGui::TextDisabled("(e.g. C:\\Users\\<you>\\AppData\\Roaming\\yip_os\\models\\)");
#else
            ImGui::Text("~/.config/yip_os/models/");
#endif
            ImGui::TextDisabled("Download models from huggingface.co/ggerganov/whisper.cpp");
            ImGui::EndTooltip();
        }
    }

    ImGui::Separator();

    // Audio device
    ImGui::Text("Audio Device");
    ImGui::TextDisabled("Select the audio source for transcription (mic or system loopback).");
    {
        static std::vector<AudioDevice> devices;
        static int selected_idx = -1;
        static bool devices_initialized = false;

        if (!devices_initialized) {
            devices = audio->GetDevices();
            std::string saved_id = config.GetState("cc.device");
            if (!saved_id.empty()) {
                for (int i = 0; i < static_cast<int>(devices.size()); i++) {
                    if (devices[i].id == saved_id) {
                        selected_idx = i;
                        audio->SetDevice(saved_id);
                        break;
                    }
                }
            }
            devices_initialized = true;
        }

        if (ImGui::Button("Refresh Devices")) {
            devices = audio->GetDevices();
            std::string cur_id = audio->GetCurrentDeviceId();
            selected_idx = -1;
            for (int i = 0; i < static_cast<int>(devices.size()); i++) {
                if (devices[i].id == cur_id) { selected_idx = i; break; }
            }
        }
        if (!devices.empty()) {
            std::string combo_preview;
            if (selected_idx >= 0 && selected_idx < static_cast<int>(devices.size()))
                combo_preview = devices[selected_idx].name;
            else
                combo_preview = audio->GetCurrentDeviceName();

            if (ImGui::BeginCombo("##cc_device", combo_preview.c_str())) {
                for (int i = 0; i < static_cast<int>(devices.size()); i++) {
                    bool is_selected = (i == selected_idx);
                    if (ImGui::Selectable(devices[i].name.c_str(), is_selected)) {
                        selected_idx = i;
                        audio->SetDevice(devices[i].id);
                        config.SetState("cc.device", devices[i].id);
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextDisabled("No devices found. Click Refresh.");
        }
    }
    ImGui::Text("Current: %s", audio->GetCurrentDeviceName().c_str());

    ImGui::Separator();

    // Window size
    ImGui::Text("Processing Window");
    int chunk_sec = whisper->GetChunkSeconds();
    if (ImGui::SliderInt("Window (sec)", &chunk_sec, 1, 10)) {
        whisper->SetChunkSeconds(chunk_sec);
        config.SetState("cc.window", std::to_string(chunk_sec));
    }
    ImGui::TextDisabled("Longer window = more accurate but slower to appear.");

    ImGui::Separator();

    // Status
    bool transcribing = whisper->IsRunning();
    bool capturing = audio->IsRunning();
    if (transcribing) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Status: LISTENING");
    } else if (whisper->IsModelLoaded()) {
        ImGui::Text("Status: Ready (navigate to CC screen to start)");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Status: No model loaded");
    }
    if (capturing) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "| Audio: CAPTURING");
    }

    // Manual stop (in case you want to stop without leaving the CC screen)
    if (transcribing) {
        if (ImGui::Button("Force Stop CC")) {
            whisper->Stop();
            audio->Stop();
        }
        ImGui::TextDisabled("CC normally stops automatically when you leave the CC screen.");
    }

    // Latest text preview
    std::string latest = whisper->PeekLatest();
    if (!latest.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("Latest: %s", latest.c_str());
    }
}

// --- Avatar Tab ---
void UIManager::RenderAvatarTab(PDAController& pda, Config& config) {
    ImGui::Text("Avatar Management");
    ImGui::TextDisabled("Browse and switch VRChat avatars via OSC. Reads avatar data");
    ImGui::TextDisabled("from VRChat's local OSC cache directory.");

    ImGui::Separator();

    if (!avtr_path_initialized_) {
        std::string initial = config.vrc_osc_path;
        if (initial.empty()) initial = YipOS::VRCAvatarData::DefaultOSCPath();
        std::snprintf(avtr_path_buf_.data(), avtr_path_buf_.size(), "%s", initial.c_str());
        avtr_path_initialized_ = true;
    }
    ImGui::InputText("VRC OSC Path", avtr_path_buf_.data(), avtr_path_buf_.size());
#ifdef _WIN32
    ImGui::TextDisabled("Default: %%LOCALAPPDATA%%Low/VRChat/VRChat/OSC/");
#else
    ImGui::TextDisabled("Linux: set to your compatdata .../VRChat/VRChat/OSC/");
#endif

    auto* avtr = pda.GetAvatarData();
    if (avtr) {
        ImGui::Text("Avatars found: %d", static_cast<int>(avtr->GetAvatars().size()));
        if (!avtr->GetCurrentAvatarId().empty()) {
            auto* current = avtr->GetCurrentAvatar();
            if (current) {
                ImGui::Text("Current: %s", current->name.c_str());
            }
        }
    }

    if (ImGui::Button("Rescan Avatars")) {
        std::string path(avtr_path_buf_.data());
        config.vrc_osc_path = path;
        if (avtr) avtr->Scan(path);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Path")) {
        config.vrc_osc_path = avtr_path_buf_.data();
        if (!config_path_.empty()) config.SaveToFile(config_path_);
    }
}

// --- Text Tab ---
void UIManager::RenderTextTab(PDAController& pda, Config& config, OSCManager& osc) {
    ImGui::Text("Text Display");
    ImGui::TextDisabled("Text appears live on the PDA TEXT screen.");

    ImGui::Separator();

    if (!text_buf_initialized_) {
        std::string saved = config.GetState("text.content");
        std::snprintf(text_buf_.data(), text_buf_.size(), "%s", saved.c_str());
        text_vrc_chatbox_ = config.GetState("text.vrc_chatbox") == "1";
        text_buf_initialized_ = true;
        // Initialize display text from saved
        pda.SetDisplayText(saved);
    }

    if (!text_vrc_chatbox_) {
        ImGui::Text("Message");
        ImGui::InputTextMultiline("##text_content", text_buf_.data(), text_buf_.size(),
                                  ImVec2(-1, 120));

        // Push text to PDAController live on every frame
        pda.SetDisplayText(std::string(text_buf_.data()));

        if (ImGui::Button("Save")) {
            config.SetState("text.content", std::string(text_buf_.data()));
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Persist to config");
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            text_buf_[0] = '\0';
            pda.SetDisplayText("");
        }
    } else {
        // Chatbox mode: show received text read-only
        std::string chatbox = osc.GetChatboxText();
        if (!chatbox.empty()) {
            ImGui::TextWrapped("Chatbox: %s", chatbox.c_str());
        } else {
            ImGui::TextDisabled("Waiting for /chatbox/input on listen port...");
        }
    }

    ImGui::Separator();

    ImGui::Text("VRChat ChatBox");
    ImGui::TextDisabled("Display text from external apps that send /chatbox/input.");
    if (ImGui::Checkbox("Use VRC ChatBox input", &text_vrc_chatbox_)) {
        config.SetState("text.vrc_chatbox", text_vrc_chatbox_ ? "1" : "0");
    }
    ImGui::TextDisabled("When enabled, listens for /chatbox/input on the OSC listen port");
    ImGui::TextDisabled("and displays that text instead of the manual input above.");
}

// --- Stocks Tab ---
void UIManager::RenderStocksTab(PDAController& pda, Config& config) {
    ImGui::Text("Stock & Crypto Monitor (STONK)");
    ImGui::TextDisabled("Real-time price graphs on the PDA. Data from Yahoo Finance (no API key).");
    ImGui::TextDisabled("Crypto symbols (BTC, DOGE, etc.) are auto-suffixed with -USD.");

    ImGui::Separator();

    // Enable checkbox
    bool enabled = config.GetState("stonk.enabled", "0") == "1";
    if (ImGui::Checkbox("Enable STONK", &enabled)) {
        config.SetState("stonk.enabled", enabled ? "1" : "0");
    }

    ImGui::Separator();

    // Symbol list
    ImGui::Text("Watched Symbols");
    std::string sym_str = config.GetState("stonk.symbols", "DOGE,BTC,AAPL,NVDA,GME");

    // Parse current symbols
    std::vector<std::string> symbols;
    {
        size_t start = 0;
        while (start < sym_str.size()) {
            size_t end = sym_str.find(',', start);
            if (end == std::string::npos) end = sym_str.size();
            std::string s = sym_str.substr(start, end - start);
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            while (!s.empty() && s.back() == ' ') s.pop_back();
            if (!s.empty()) symbols.push_back(s);
            start = end + 1;
        }
    }

    // Display list with remove buttons
    int remove_idx = -1;
    for (int i = 0; i < static_cast<int>(symbols.size()); i++) {
        ImGui::BulletText("%s", symbols[i].c_str());
        ImGui::SameLine(200);
        std::string btn_id = "Remove##sym_" + std::to_string(i);
        if (ImGui::SmallButton(btn_id.c_str())) {
            remove_idx = i;
        }
    }
    if (remove_idx >= 0) {
        symbols.erase(symbols.begin() + remove_idx);
        std::string new_str;
        for (int i = 0; i < static_cast<int>(symbols.size()); i++) {
            if (i > 0) new_str += ',';
            new_str += symbols[i];
        }
        config.SetState("stonk.symbols", new_str);
    }

    // Add symbol
    ImGui::InputText("##add_sym", stonk_symbol_buf_.data(), stonk_symbol_buf_.size());
    ImGui::SameLine();
    if (ImGui::Button("Add")) {
        std::string new_sym(stonk_symbol_buf_.data());
        // Trim and uppercase
        while (!new_sym.empty() && new_sym.front() == ' ') new_sym.erase(new_sym.begin());
        while (!new_sym.empty() && new_sym.back() == ' ') new_sym.pop_back();
        for (auto& c : new_sym) c = static_cast<char>(toupper(c));
        if (!new_sym.empty()) {
            symbols.push_back(new_sym);
            std::string new_str;
            for (int i = 0; i < static_cast<int>(symbols.size()); i++) {
                if (i > 0) new_str += ',';
                new_str += symbols[i];
            }
            config.SetState("stonk.symbols", new_str);
            stonk_symbol_buf_[0] = '\0';
        }
    }

    ImGui::Separator();

    // Refresh interval
    ImGui::Text("Refresh Interval");
    std::string refresh_str = config.GetState("stonk.refresh", "300");
    int refresh_sec = 300;
    try { refresh_sec = std::stoi(refresh_str); }
    catch (...) {}
    if (ImGui::SliderInt("Seconds##stonk_refresh", &refresh_sec, 60, 900)) {
        config.SetState("stonk.refresh", std::to_string(refresh_sec));
    }
    ImGui::TextDisabled("How often to fetch new price data (60s - 900s).");

    // Manual fetch
    if (ImGui::Button("Fetch Now")) {
        pda.ReloadStockSymbols();
        auto* client = pda.GetStockClient();
        if (client) {
            std::string window = config.GetState("stonk.window", "1MO");
            client->FetchAll(window);
        }
    }

    ImGui::Separator();

    // Show current data if available
    auto* client = pda.GetStockClient();
    if (client && !client->GetQuotes().empty()) {
        ImGui::Text("Cached Data");
        for (auto& q : client->GetQuotes()) {
            float pct = 0;
            if (q.prev_close > 0) pct = ((q.current_price - q.prev_close) / q.prev_close) * 100.0f;
            ImGui::Text("  %s: $%.4f (%+.1f%%)", q.symbol.c_str(), q.current_price, pct);
        }
    }
}

// --- NVRAM Tab ---
void UIManager::RenderNVRAMTab(PDAController& pda, Config& config) {
    ImGui::Text("NVRAM (Persistent State)");
    ImGui::TextDisabled("Key-value store saved to config.ini [state] section.");
    ImGui::TextDisabled("Used by screens to remember preferences across restarts.");

    ImGui::Separator();

    ImGui::Text("%d key%s stored", static_cast<int>(config.state.size()),
                config.state.size() == 1 ? "" : "s");

    if (!config.state.empty()) {
        ImGui::Separator();
        for (auto& [key, val] : config.state) {
            ImGui::BulletText("%s = %s", key.c_str(), val.c_str());
        }
        ImGui::Separator();
        if (ImGui::Button("Clear All NVRAM")) {
            config.ClearState();
        }
        ImGui::TextDisabled("This will reset all saved preferences (disk, network, CC settings, etc.).");
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

void UIManager::TickSimulations(PDAController& pda) {
    double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Heart rate auto-sim
    if (sim_hr_auto_) {
        static double last_hr_send = 0;
        if (now - last_hr_send >= 1.0) {
            static int sim_hr_bpm = 75;
            int jitter = (static_cast<int>(now * 7) % 7) - 3;
            int bpm = std::clamp(sim_hr_bpm + jitter, 40, 200);
            pda.SetHeartRate(bpm);
            last_hr_send = now;
        }
    }

    // BFI auto-sim
    if (sim_bfi_auto_) {
        static double last_bfi_send = 0;
        static double bfi_start_time = 0;
        if (bfi_start_time == 0) bfi_start_time = now;
        if (now - last_bfi_send >= 1.0) {
            double t = now - bfi_start_time;  // relative time, keeps sin args small
            for (int i = 0; i < PDAController::BFI_PARAM_COUNT; i++) {
                bool is_pos = PDAController::BFI_PARAMS[i].positive_only;
                float lo = is_pos ? 0.0f : -1.0f;
                float hi = 1.0f;
                float mid = (hi + lo) * 0.5f;
                float amp = (hi - lo) * 0.5f;
                // 20-second full cycle, phase-shifted per param
                float val = mid + amp * static_cast<float>(
                    std::sin(t * (2.0 * 3.14159265358979 / 20.0) + i * 1.7));
                val = std::clamp(val, lo, 1.0f);
                pda.SetBFIParam(i, val);
            }
            last_bfi_send = now;
        }
    }
}

} // namespace YipOS
