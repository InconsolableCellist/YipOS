#include "UIManager.hpp"
#include "app/PDAController.hpp"
#include "core/Config.hpp"

#include <imgui.h>

namespace YipOS {

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

} // namespace YipOS
