/**
 * UIManager_Shock.cpp
 * V1.0.0
 *
 * Shock device configuration screen for YipOS.
 *
 * By otter_oasis
 */

#include "UIManager.hpp"
#include "app/PDAController.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "net/OpenShockClient.hpp"
#include "net/PiShockClient.hpp"
#include "net/ShockManager.hpp"

#include <algorithm>
#include <cstdio>
#include <imgui.h>

namespace YipOS {

void UIManager::RenderShockTab(PDAController &pda, Config &config) {
  ImGui::Text("Shocker Integration");
  ImGui::TextDisabled(
      "Drive your PiShock & OpenShock devices directly from the PDA.");
  ImGui::Spacing();

  ImGui::TextDisabled("Warning: Using shocking devices is at your own risk.");
  ImGui::TextDisabled("Use responsibly and follow all safety guidelines.");
  ImGui::TextDisabled(
      "Remember: If other people can interact with your PDA, they can "
      "control your shocks.");

  ImGui::Separator();
  ImGui::Spacing();

  auto *manager = pda.GetShockManager();
  if (!manager) {
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                       "ShockManager not initialized.");
    return;
  }

  // --- Initialize Config Buffers ---
  if (!openshock_token_initialized_ || !pishock_initialized_) {
    // OpenShock
    std::string os_enabled = config.GetState("openshock.enabled", "0");
    openshock_enabled_ = (os_enabled != "0");

    std::string token = config.GetState("openshock.token");
    std::snprintf(openshock_token_buf_.data(), openshock_token_buf_.size(),
                  "%s", token.c_str());

    // PiShock
    std::string ps_enabled = config.GetState("pishock.enabled", "0");
    pishock_enabled_ = (ps_enabled != "0");

    std::string ps_user = config.GetState("pishock.username");
    std::snprintf(pishock_username_buf_.data(), pishock_username_buf_.size(),
                  "%s", ps_user.c_str());

    std::string ps_api = config.GetState("pishock.apikey");
    std::snprintf(pishock_apikey_buf_.data(), pishock_apikey_buf_.size(), "%s",
                  ps_api.c_str());

    std::string i_step = config.GetState("shock.intensity_step", "2.5");
    try {
      shock_intensity_step_ = std::stof(i_step);
    } catch (...) {
    }
    std::string d_step = config.GetState("shock.duration_step", "1000");
    try {
      shock_duration_step_ = std::stoi(d_step);
    } catch (...) {
    }

    openshock_token_initialized_ = true;
    pishock_initialized_ = true;
  }

  // --- PiShock Config ---
  ImGui::Text("PiShock Configuration");
  ImGui::Checkbox("Enable PiShock", &pishock_enabled_);

  if (!pishock_enabled_) {
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Status: Disabled");
  } else {
    auto *ps = manager->GetPiShockClient();
    if (ps && ps->HasConfig()) {
      if (ps->IsTokenValid()) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Status: Verified");
      } else {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                           "Status: Auth Pending/Failed");
      }
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                         "Status: Unconfigured");
    }
  }

  ImGui::SetNextItemWidth(150);
  ImGui::InputText("Username", pishock_username_buf_.data(),
                   pishock_username_buf_.size());
  ImGui::SetNextItemWidth(-1);
  ImGui::InputText("##ps_token", pishock_apikey_buf_.data(),
                   pishock_apikey_buf_.size(), ImGuiInputTextFlags_Password);
  ImGui::TextDisabled("API Key generated at https://login.pishock.com/account");

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // --- OpenShock Config ---
  ImGui::Text("OpenShock Configuration");
  ImGui::Checkbox("Enable OpenShock", &openshock_enabled_);

  if (!openshock_enabled_) {
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Status: Disabled");
  } else {
    auto *os = manager->GetOpenShockClient();
    if (os && os->HasConfig()) {
      if (os->IsTokenValid()) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Status: Verified");
      } else {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                           "Status: Auth Pending/Failed");
      }
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                         "Status: Unconfigured");
    }
  }

  ImGui::SetNextItemWidth(-1);
  ImGui::InputText("##os_token", openshock_token_buf_.data(),
                   openshock_token_buf_.size(), ImGuiInputTextFlags_Password);
  ImGui::TextDisabled(
      "API Token generated at https://next.openshock.app/settings/api-tokens");

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // --- Global Wrist Screen Controls ---
  ImGui::Text("Wrist Screen Controls");
  ImGui::TextDisabled(
      "Step sizes used by wrist buttons for all shock devices.");
  ImGui::SetNextItemWidth(150);
  ImGui::InputFloat("Intensity Step (%)", &shock_intensity_step_, 0.5f,
                    5.0f, "%.1f");
  ImGui::SetNextItemWidth(150);
  ImGui::InputInt("Duration Step (ms)", &shock_duration_step_, 100, 1000);

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // --- Actions ---
  if (ImGui::Button("Apply & Save Settings")) {
    // OpenShock string trim
    std::string os_token(openshock_token_buf_.data());
    os_token.erase(
        std::remove_if(os_token.begin(), os_token.end(),
                       [](unsigned char ch) { return std::isspace(ch); }),
        os_token.end());

    std::string ps_api(pishock_apikey_buf_.data());
    ps_api.erase(
        std::remove_if(ps_api.begin(), ps_api.end(),
                       [](unsigned char ch) { return std::isspace(ch); }),
        ps_api.end());

    std::string ps_user(pishock_username_buf_.data());
    ps_user.erase(
        std::remove_if(ps_user.begin(), ps_user.end(),
                       [](unsigned char ch) { return std::isspace(ch); }),
        ps_user.end());

    config.SetState("openshock.enabled", openshock_enabled_ ? "1" : "0");
    config.SetState("openshock.token", os_token);
    config.SetState("shock.intensity_step",
                    std::to_string(shock_intensity_step_));
    config.SetState("shock.duration_step",
                    std::to_string(shock_duration_step_));

    config.SetState("pishock.enabled", pishock_enabled_ ? "1" : "0");
    config.SetState("pishock.username", ps_user);
    config.SetState("pishock.apikey", ps_api);

    manager->InitFromConfig(config);

    if (!config_path_.empty())
      config.SaveToFile(config_path_);
    Logger::Info("Shock manager settings updated.");
  }

  ImGui::Separator();
  ImGui::Text("Tools");

  if (ImGui::Button("Refresh Shocker List")) {
    manager->FetchShockers();
  }

  ImGui::SameLine();

  if (ImGui::Button("Test Vibration")) {
    const auto &shockers = manager->GetShockers();
    if (!shockers.empty()) {
      manager->SendControl(shockers[0].id, shockers[0].backend, "Vibrate",
                           20.0f, 1000);
      Logger::Info("ShockManager: Sent test vibration to " + shockers[0].name);
    } else {
      Logger::Warning("ShockManager: No shockers available to test.");
    }
  }

  // --- Footer ---
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::TextDisabled("Module by @otter_oasis");
}

} // namespace YipOS
