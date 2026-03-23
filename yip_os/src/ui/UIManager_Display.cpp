#include "UIManager.hpp"
#include "app/PDAController.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"

#include <imgui.h>

namespace YipOS {

void UIManager::RenderDisplayTab(PDAController& pda, Config& config) {
    ImGui::Text("Display & Timing");
    ImGui::TextDisabled("Calibrate the Williams Tube write head position and timing.");

    ImGui::Separator();

    ImGui::Text("Y Calibration");
    ImGui::TextDisabled("Adjusts vertical positioning of text on the CRT display.");
    ImGui::SliderFloat("Y Offset", &config.y_offset, -0.5f, 0.5f);
    ImGui::SameLine(); if (ImGui::SmallButton("Reset##yoff")) config.y_offset = 0.0f;
    ImGui::SliderFloat("Y Scale", &config.y_scale, 0.1f, 2.0f);
    ImGui::SameLine(); if (ImGui::SmallButton("Reset##yscl")) config.y_scale = 1.0f;
    ImGui::SliderFloat("Y Curve", &config.y_curve, 0.1f, 3.0f);
    ImGui::SameLine(); if (ImGui::SmallButton("Reset##ycur")) config.y_curve = 1.0f;

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

} // namespace YipOS
