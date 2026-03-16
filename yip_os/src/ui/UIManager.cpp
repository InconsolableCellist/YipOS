#include "UIManager.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "app/ScreenBuffer.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "core/Glyphs.hpp"
#include "net/OSCManager.hpp"
#include "net/VRCXData.hpp"
#include "net/VRCAvatarData.hpp"
#include "audio/AudioCapture.hpp"
#include "audio/WhisperWorker.hpp"
#include "screens/Screen.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>
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
    auto& display = pda.GetDisplay();

    // --- Header ---
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "YIP-BOI OS v1.0");
    ImGui::SameLine();
    ImGui::TextDisabled("(C) Foxipso 2026");
    ImGui::TextDisabled("Enable the PDA and Williams Tube in VRChat to see the output!");
    ImGui::Text("OS: Up and running");
    ImGui::Separator();

    // --- Input Controls ---
    ImVec2 nav_btn(60, 28);
    ImVec2 touch_btn(40, 28);

    // Row 1: HOME | touch row 1 | SEL
    if (ImGui::Button("HOME", nav_btn)) pda.QueueInput("TL");
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

    // Row 2: PG UP | touch row 2 | JOY DWN (tall)
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
    ImGui::SameLine(0, 8);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0, 16);
    if (ImGui::Button("JOY\nDWN", ImVec2(nav_btn.x, nav_btn.y * 2 + 4))) pda.QueueInput("Joystick");

    // Row 3: PG DWN | touch row 3
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
    ImGui::Text("VRCX Integration");
    ImGui::TextDisabled("Reads world history, feed, etc. from VRCX's local database.");

    if (ImGui::Checkbox("Enable VRCX", &config.vrcx_enabled)) {
        // Auto-save immediately so the setting persists across restarts
        if (!config_path_.empty()) {
            config.SaveToFile(config_path_);
        }
    }

    if (config.vrcx_enabled) {
        // Initialize path buffer from config on first frame
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
            if (!config_path_.empty()) {
                config.SaveToFile(config_path_);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Disconnect")) {
            if (vrcx) vrcx->Close();
        }
    } else {
        vrcx_path_initialized_ = false;
        VRCXData* vrcx = pda.GetVRCXData();
        if (vrcx && vrcx->IsOpen()) {
            vrcx->Close();
        }
    }

    // --- CC (Closed Captions) ---
    ImGui::Separator();
    ImGui::Text("Closed Captions (CC)");
    ImGui::TextDisabled("Live transcription via whisper.cpp + audio capture.");

    auto* whisper = pda.GetWhisperWorker();
    auto* audio = pda.GetAudioCapture();

    if (whisper && audio) {
        // Model status
        if (whisper->IsModelLoaded()) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Model: %s", whisper->GetModelName().c_str());
        } else {
            std::string saved = config.GetState("cc.model");
            if (!saved.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Model: %s (not loaded)", saved.c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Model: not loaded");
            }
        }

        // Model loading buttons
        if (ImGui::Button("Load tiny.en")) {
            if (whisper->LoadModel(WhisperWorker::DefaultModelPath("tiny.en"))) {
                config.SetState("cc.model", whisper->GetModelName());
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load base.en")) {
            if (whisper->LoadModel(WhisperWorker::DefaultModelPath("base.en"))) {
                config.SetState("cc.model", whisper->GetModelName());
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Place models in config/models/");

        // Audio device selection — auto-enumerate on first view and restore saved selection
        ImGui::Text("Audio Device:");
        {
            static std::vector<AudioDevice> devices;
            static int selected_idx = -1;
            static bool devices_initialized = false;

            if (!devices_initialized) {
                devices = audio->GetDevices();
                // Restore saved device
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
                // Try to keep current selection
                std::string cur_id = audio->GetCurrentDeviceId();
                selected_idx = -1;
                for (int i = 0; i < static_cast<int>(devices.size()); i++) {
                    if (devices[i].id == cur_id) {
                        selected_idx = i;
                        break;
                    }
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
                ImGui::TextDisabled("No devices found");
            }
        }

        ImGui::Text("Current: %s", audio->GetCurrentDeviceName().c_str());

        // Chunk window size
        int chunk_sec = whisper->GetChunkSeconds();
        if (ImGui::SliderInt("Window (sec)", &chunk_sec, 2, 10)) {
            whisper->SetChunkSeconds(chunk_sec);
            config.SetState("cc.window", std::to_string(chunk_sec));
        }
        ImGui::TextDisabled("Longer = more accurate but slower to appear");

        // Status
        bool capturing = audio->IsRunning();
        bool transcribing = whisper->IsRunning();
        if (transcribing) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Status: LISTENING");
        } else if (whisper->IsModelLoaded()) {
            ImGui::Text("Status: Ready");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Status: No model");
        }

        if (capturing) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), " | Audio: CAPTURING");
        }

        // Start/Stop
        if (!transcribing) {
            if (ImGui::Button("Start CC")) {
                if (!whisper->IsModelLoaded()) {
                    std::string saved = config.GetState("cc.model", "tiny.en");
                    whisper->LoadModel(WhisperWorker::DefaultModelPath(saved));
                }
                if (whisper->IsModelLoaded()) {
                    audio->Start();
                    whisper->Start(audio->GetBuffer());
                }
            }
        } else {
            if (ImGui::Button("Stop CC")) {
                whisper->Stop();
                audio->Stop();
            }
        }

        // Latest text preview
        std::string latest = whisper->PeekLatest();
        if (!latest.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("Latest: %s", latest.c_str());
        }
    }

    // --- Avatar Management ---
    ImGui::Separator();
    ImGui::Text("Avatar Management");

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

    ImGui::Separator();
    if (ImGui::Button("Save Config")) {
        config.osc_ip = ip_buf;
        if (vrcx_path_initialized_) {
            config.vrcx_db_path = vrcx_path_buf_.data();
        }
        if (avtr_path_initialized_) {
            config.vrc_osc_path = avtr_path_buf_.data();
        }
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

    ImGui::Separator();
    ImGui::Text("NVRAM (%d key%s)", static_cast<int>(config.state.size()),
                config.state.size() == 1 ? "" : "s");
    if (!config.state.empty()) {
        for (auto& [key, val] : config.state) {
            ImGui::BulletText("%s = %s", key.c_str(), val.c_str());
        }
        if (ImGui::Button("Clear NVRAM")) {
            config.ClearState();
        }
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
