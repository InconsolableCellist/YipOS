#include "UIManager.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "app/ScreenBuffer.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "core/Glyphs.hpp"
#include "net/OSCManager.hpp"
#include "net/VRCXData.hpp"
#include "audio/AudioCapture.hpp"
#include "audio/WhisperWorker.hpp"
#include "screens/Screen.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <cstdio>
#include <chrono>
#include <algorithm>
#include <filesystem>

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

    // Try to load macro atlas from assets path
    if (!assets_path_.empty()) {
        LoadMacroAtlas(assets_path_ + "/WilliamsTube_MacroAtlas.png");
    }

    Logger::Info("UI initialized: " + title);
    return true;
}

void UIManager::Shutdown() {
    if (!window_) return;

    if (macro_atlas_tex_) {
        glDeleteTextures(1, &macro_atlas_tex_);
        macro_atlas_tex_ = 0;
        macro_atlas_loaded_ = false;
    }

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

void UIManager::HandleKeyboardShortcuts(PDAController& pda) {
    // Skip if any text input is active (typing in a field)
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;

    bool shift = io.KeyShift;
    bool ctrl = io.KeyCtrl;

    // 1-5: touch row 1, Shift+1-5: touch row 2, QWERT: touch row 3
    ImGuiKey numkeys[] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5};
    for (int i = 0; i < 5; i++) {
        if (ImGui::IsKeyPressed(numkeys[i], false)) {
            int col = i + 1;
            int row = shift ? 2 : 1;
            std::string input = std::to_string(col) + std::to_string(row);
            pda.QueueInput(input);
            return;
        }
    }

    ImGuiKey row3keys[] = {ImGuiKey_Q, ImGuiKey_W, ImGuiKey_E, ImGuiKey_R, ImGuiKey_T};
    for (int i = 0; i < 5; i++) {
        if (ImGui::IsKeyPressed(row3keys[i], false)) {
            std::string input = std::to_string(i + 1) + "3";
            pda.QueueInput(input);
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

// --- Atlas Loading ---

bool UIManager::LoadMacroAtlas(const std::string& path) {
    int w, h, channels;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 1);
    if (!data) {
        Logger::Warning("Could not load macro atlas: " + path);
        return false;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data);

    // Swizzle so single-channel reads as (R,R,R,R) — white foreground
    GLint swizzle[] = {GL_RED, GL_RED, GL_RED, GL_RED};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);

    stbi_image_free(data);

    macro_atlas_tex_ = tex;
    macro_atlas_loaded_ = true;
    Logger::Info("Macro atlas loaded: " + path + " (" + std::to_string(w) + "x" + std::to_string(h) + ")");
    return true;
}

// --- Tab Implementations ---

void UIManager::RenderStatusTab(PDAController& pda, OSCManager& osc) {
    if (pda.IsBooting()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "BOOTING...");
        return;
    }

    Screen* current = pda.GetCurrentScreen();
    std::string screen_name = current ? current->name : "NONE";
    int remaining = pda.GetDisplay().BufferedRemaining();
    ImGui::Text("Screen: %s  Writes: %d", screen_name.c_str(), remaining);

    ImGui::Separator();

    // --- Side-by-side: touch preview (left) + screen buffer (right) ---
    RenderScreenPreview(pda);

    ImGui::Separator();

    // OSC incoming
    ImGui::Text("OSC Incoming");
    ImGui::BeginChild("OSCRecv", ImVec2(0, 0), true);
    auto recvs = osc.GetRecentRecvs();
    for (auto it = recvs.rbegin(); it != recvs.rend() && std::distance(recvs.rbegin(), it) < 20; ++it) {
        ImGui::Text("  %s = %.2f", it->path.c_str(), it->value);
    }
    ImGui::EndChild();
}

void UIManager::RenderScreenPreview(PDAController& pda) {
    Screen* current = pda.GetCurrentScreen();
    int macro_index = current ? current->macro_index : -1;

    float preview_size = 200.0f;
    ImVec2 img_size(preview_size, preview_size);

    // Left column: touch preview + nav buttons
    ImGui::BeginGroup();
    ImGui::TextDisabled("Touch Preview (click to input)");

    if (macro_atlas_loaded_ && macro_index >= 0) {
        int grid_col = macro_index % 8;
        int grid_row = macro_index / 8;
        ImVec2 uv0(grid_col / 8.0f, grid_row / 8.0f);
        ImVec2 uv1((grid_col + 1) / 8.0f, (grid_row + 1) / 8.0f);
        ImVec4 tint(0.2f, 1.0f, 0.4f, 1.0f);
        ImVec4 bg(0.0f, 0.0f, 0.0f, 1.0f);
        ImGui::Image(
            reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(macro_atlas_tex_)),
            img_size, uv0, uv1, tint, bg);
    } else {
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            cursor, ImVec2(cursor.x + img_size.x, cursor.y + img_size.y),
            IM_COL32(0, 0, 0, 255));
        ImGui::Dummy(img_size);
    }

    // Handle clicks on the preview
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
        ImVec2 mouse = ImGui::GetMousePos();
        ImVec2 rect_min = ImGui::GetItemRectMin();
        ImVec2 rect_size = ImGui::GetItemRectSize();
        float nx = (mouse.x - rect_min.x) / rect_size.x;
        float ny = (mouse.y - rect_min.y) / rect_size.y;
        HandlePreviewClick(pda, nx, ny);
    }

    // Nav buttons
    if (ImGui::Button("HOME")) pda.QueueInput("TL");
    ImGui::SameLine();
    if (ImGui::Button("BACK")) pda.QueueInput("ML");
    ImGui::SameLine();
    if (ImGui::Button("JOY")) pda.QueueInput("Joystick");
    ImGui::SameLine();
    if (ImGui::Button("TR")) pda.QueueInput("TR");

    ImGui::EndGroup();

    // Right column: screen buffer text
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::TextDisabled("Screen Buffer (dynamic text)");
    std::string dump = pda.GetDisplay().GetScreen().Dump();
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::TextUnformatted(dump.c_str());
    ImGui::PopFont();
    ImGui::EndGroup();
}

void UIManager::HandlePreviewClick(PDAController& pda, float nx, float ny) {
    using namespace Glyphs;

    // Map normalized position to grid coordinates
    int col = static_cast<int>(nx * COLS);
    int row = static_cast<int>(ny * ROWS);
    col = std::clamp(col, 0, COLS - 1);
    row = std::clamp(row, 0, ROWS - 1);

    // Row 7 (status bar) and row 0 (title bar) — not interactive
    if (row == 0 || row == 7) return;

    // Map to 5x3 touch zone grid
    int tile_col = col / CHARS_PER_TILE; // 0-4
    tile_col = std::clamp(tile_col, 0, TILE_COLS - 1);

    // Map row to zone: ZONE_ROWS are {1, 4, 6}
    // Rows 1-3 → zone 0, rows 4-5 → zone 1, row 6 → zone 2
    int tile_row;
    if (row <= 3) tile_row = 0;
    else if (row <= 5) tile_row = 1;
    else tile_row = 2;

    // Format: "12" = col 1, row 2 (1-indexed)
    std::string suffix = std::to_string(tile_col + 1) + std::to_string(tile_row + 1);
    Logger::Debug("Preview click: grid(" + std::to_string(col) + "," + std::to_string(row) +
                  ") -> touch " + suffix);
    pda.QueueInput(suffix);
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
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Model: not loaded");
        }

        // Model loading buttons
        if (ImGui::Button("Load tiny.en")) {
            whisper->LoadModel(WhisperWorker::DefaultModelPath("tiny.en"));
        }
        ImGui::SameLine();
        if (ImGui::Button("Load base.en")) {
            whisper->LoadModel(WhisperWorker::DefaultModelPath("base.en"));
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Place models in config/models/");

        // Audio device selection
        ImGui::Text("Audio Device:");
        {
            static std::vector<AudioDevice> devices;
            static int selected_idx = 0;
            if (ImGui::Button("Refresh Devices")) {
                devices = audio->GetDevices();
                selected_idx = 0;
            }
            if (!devices.empty()) {
                // Build combo items
                static std::string combo_preview;
                if (selected_idx < static_cast<int>(devices.size()))
                    combo_preview = devices[selected_idx].name;
                else
                    combo_preview = "Select...";

                if (ImGui::BeginCombo("##cc_device", combo_preview.c_str())) {
                    for (int i = 0; i < static_cast<int>(devices.size()); i++) {
                        bool is_selected = (i == selected_idx);
                        if (ImGui::Selectable(devices[i].name.c_str(), is_selected)) {
                            selected_idx = i;
                            audio->SetDevice(devices[i].id);
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::TextDisabled("Click 'Refresh Devices' to enumerate");
            }
        }

        ImGui::Text("Current: %s", audio->GetCurrentDeviceName().c_str());

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
                    whisper->LoadModel(WhisperWorker::DefaultModelPath("tiny.en"));
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

    ImGui::Separator();
    if (ImGui::Button("Save Config")) {
        config.osc_ip = ip_buf;
        if (vrcx_path_initialized_) {
            config.vrcx_db_path = vrcx_path_buf_.data();
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
