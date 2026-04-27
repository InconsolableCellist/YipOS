#include "git_hash.h"
#include "UIManager.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "net/OSCManager.hpp"
#include "net/BridgeClient.hpp"
#include "audio/WhisperWorker.hpp"
#include "core/Config.hpp"
#include "screens/Screen.hpp"

#include <imgui.h>
#include <cstdio>
#include <chrono>

namespace YipOS {

void UIManager::RenderStatusTab(PDAController& pda, OSCManager& osc) {
    auto& display = pda.GetDisplay();

    // --- Header ---
    std::string header = std::string("YIP OS v") + YIP_VERSION + " (" + YIP_GIT_HASH + ")";
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "%s", header.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(C) Foxipso 2026");
    ImGui::TextDisabled("Enable the PDA in VRChat to see the output!");
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

    // --- YC Bridge ---
    {
        auto& config = pda.GetConfig();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Yip Companion Bridge");

        bool bridge_enabled = config.GetState("bridge.enabled") == "1";
        if (ImGui::Checkbox("Enable##bridge", &bridge_enabled)) {
            config.SetState("bridge.enabled", bridge_enabled ? "1" : "0");
            auto* bc = pda.GetBridgeClient();
            if (bridge_enabled && bc && !bc->IsConnected()) {
                std::string host = config.GetState("bridge.host");
                if (host.empty()) host = "127.0.0.1";
                int port = 9200;
                std::string pv = config.GetState("bridge.port");
                if (!pv.empty()) {
                    try { port = std::stoi(pv); } catch (...) {}
                }
                bc->Stop();
                bc->Start(host, port);
            } else if (!bridge_enabled && bc) {
                bc->Stop();
            }
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        static char host_buf[64] = {};
        static bool host_init = false;
        if (!host_init) {
            std::string hv = config.GetState("bridge.host");
            if (hv.empty()) hv = "127.0.0.1";
            snprintf(host_buf, sizeof(host_buf), "%s", hv.c_str());
            host_init = true;
        }
        if (ImGui::InputText("Host##bridge", host_buf, sizeof(host_buf))) {
            config.SetState("bridge.host", host_buf);
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        static char port_buf[8] = {};
        static bool port_init = false;
        if (!port_init) {
            std::string pv = config.GetState("bridge.port");
            if (pv.empty()) pv = "9200";
            snprintf(port_buf, sizeof(port_buf), "%s", pv.c_str());
            port_init = true;
        }
        if (ImGui::InputText("Port##bridge", port_buf, sizeof(port_buf),
                             ImGuiInputTextFlags_CharsDecimal)) {
            config.SetState("bridge.port", port_buf);
        }

        auto* bc = pda.GetBridgeClient();
        if (bc) {
            ImGui::SameLine();
            if (bc->IsConnected()) {
                auto data = bc->GetData();
                if (!data.companion_name.empty()) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f),
                                       "Connected (%s)", data.companion_name.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Connected");
                }
            } else if (bridge_enabled) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "Connecting...");
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Disabled");
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

} // namespace YipOS
