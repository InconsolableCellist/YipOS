#include "UIManager.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "net/OSCManager.hpp"
#include "screens/BFIData.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>
#include <cmath>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef APIENTRY // avoid redefinition warning with glad.h
#endif

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
        if (ImGui::BeginTabItem("INTRP")) {
            RenderINTRPTab(pda, config);
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
        if (ImGui::BeginTabItem("IMG")) {
            RenderIMGTab(pda, config);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Stocks")) {
            RenderStocksTab(pda, config);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Twitch")) {
            RenderTwitchTab(pda, config);
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
            for (int i = 0; i < BFI_PARAM_COUNT; i++) {
                bool is_pos = BFI_PARAMS[i].positive_only;
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
