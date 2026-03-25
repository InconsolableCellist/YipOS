#include "UIManager.hpp"
#include "app/PDAController.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "net/DMClient.hpp"

#include <imgui.h>
#include <cstdio>
#include <string>

namespace YipOS {

void UIManager::RenderDMTab(PDAController& pda, Config& config) {
    ImGui::Text("Private DM");
    ImGui::TextDisabled("Send private messages to other Yip-Boi users.");
    ImGui::TextDisabled("Pair with a friend using a 6-digit code, then chat.");

    ImGui::Separator();

    auto& client = pda.GetDMClient();

    // Endpoint config
    if (!dm_endpoint_initialized_) {
        std::string ep = config.GetState("dm.endpoint",
            "https://yipos-dm.dan-a7b.workers.dev");
        std::snprintf(dm_endpoint_buf_.data(), dm_endpoint_buf_.size(), "%s", ep.c_str());
        dm_endpoint_initialized_ = true;
    }
    ImGui::InputText("Worker URL", dm_endpoint_buf_.data(), dm_endpoint_buf_.size());
    ImGui::SameLine();
    if (ImGui::Button("Apply##dm_ep")) {
        std::string ep(dm_endpoint_buf_.data());
        config.SetState("dm.endpoint", ep);
        client.SetEndpoint(ep);
    }

    // Display name
    if (!dm_name_initialized_) {
        std::string name = config.GetState("dm.display_name", "Yip User");
        std::snprintf(dm_name_buf_.data(), dm_name_buf_.size(), "%s", name.c_str());
        dm_name_initialized_ = true;
    }
    ImGui::InputText("Display Name", dm_name_buf_.data(), dm_name_buf_.size());
    ImGui::SameLine();
    if (ImGui::Button("Save##dm_name")) {
        std::string name(dm_name_buf_.data());
        config.SetState("dm.display_name", name);
        client.SetDisplayName(name);
    }

    ImGui::TextDisabled("User ID: %s", client.GetUserId().c_str());

    ImGui::Separator();

    // --- Pairing ---
    ImGui::Text("Pairing");

    auto& pair = client.GetPairInfo();

    // Create pairing session
    if (ImGui::Button("Create Pair Code")) {
        std::string code, sid;
        if (client.PairCreate(code, sid)) {
            pair.state = PairState::WAITING;
            pair.code = code;
            pair.session_id = sid;
            Logger::Info("DM pair code created: " + code);
        } else {
            pair.state = PairState::FAILED;
            pair.error = "Network error";
        }
    }

    // Show active code
    if (pair.state == PairState::WAITING) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f),
                           "Code: %s", pair.code.c_str());
        ImGui::TextDisabled("Share this code with your friend");
    }

    ImGui::Spacing();

    // Join with code
    ImGui::InputText("##dm_join_code", dm_join_code_buf_.data(), dm_join_code_buf_.size());
    ImGui::SameLine();
    if (ImGui::Button("Join")) {
        std::string code(dm_join_code_buf_.data());
        // Trim
        while (!code.empty() && code.front() == ' ') code.erase(code.begin());
        while (!code.empty() && code.back() == ' ') code.pop_back();

        if (!code.empty()) {
            std::string sid, peer;
            if (client.PairJoin(code, sid, peer)) {
                // Auto-confirm
                if (client.PairConfirm(sid)) {
                    client.AddSession(sid, "", peer);
                    pda.SaveDMSessions();
                    pair.state = PairState::COMPLETE;
                    pair.session_id = sid;
                    pair.peer_name = peer;
                    dm_join_code_buf_[0] = '\0';
                    Logger::Info("DM paired with " + peer);
                } else {
                    pair.state = PairState::FAILED;
                    pair.error = "Confirm failed";
                }
            } else {
                pair.state = PairState::FAILED;
                pair.error = "Invalid code or expired";
            }
        }
    }
    ImGui::TextDisabled("Enter a 6-digit code from a friend");

    if (pair.state == PairState::COMPLETE) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f),
                           "Paired with %s!", pair.peer_name.c_str());
    } else if (pair.state == PairState::FAILED) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                           "Error: %s", pair.error.c_str());
    }

    ImGui::Separator();

    // --- Conversations ---
    ImGui::Text("Conversations");

    auto& sessions = client.GetSessions();
    if (sessions.empty()) {
        ImGui::TextDisabled("No conversations yet. Pair with a friend above.");
    }

    std::string remove_sid;
    for (auto& s : sessions) {
        ImGui::PushID(s.session_id.c_str());

        bool has_unseen = s.has_unseen;
        if (has_unseen) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "*");
            ImGui::SameLine();
        }
        ImGui::Text("%s", s.peer_name.c_str());

        // Last message preview
        if (!s.messages.empty()) {
            auto& last = s.messages[0];
            std::string preview = last.text;
            if (preview.size() > 40) preview = preview.substr(0, 37) + "...";
            ImGui::SameLine(150);
            ImGui::TextDisabled("%s", preview.c_str());
        }

        // Compose
        std::string compose_id = "##compose_" + s.session_id;
        auto it = dm_compose_bufs_.find(s.session_id);
        if (it == dm_compose_bufs_.end()) {
            dm_compose_bufs_[s.session_id] = {};
            dm_compose_bufs_[s.session_id][0] = '\0';
            it = dm_compose_bufs_.find(s.session_id);
        }

        ImGui::InputText(compose_id.c_str(), it->second.data(), it->second.size());
        ImGui::SameLine();
        if (ImGui::Button("Send")) {
            std::string text(it->second.data());
            if (!text.empty()) {
                if (client.SendMessage(s.session_id, text)) {
                    it->second[0] = '\0';
                    client.FetchMessages(s.session_id, 0);
                    Logger::Info("DM sent to " + s.peer_name);
                } else {
                    Logger::Warning("DM send failed to " + s.peer_name);
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            remove_sid = s.session_id;
        }

        ImGui::PopID();
        ImGui::Spacing();
    }

    if (!remove_sid.empty()) {
        client.RemoveSession(remove_sid);
        dm_compose_bufs_.erase(remove_sid);
        pda.SaveDMSessions();
    }

    ImGui::Separator();

    // Manual poll
    if (ImGui::Button("Refresh All")) {
        client.PollAll();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Auto-polls every 60s");

    // Poll interval
    std::string poll_str = config.GetState("dm.poll_interval", "60");
    int poll_sec = 60;
    try { poll_sec = std::stoi(poll_str); } catch (...) {}
    if (ImGui::SliderInt("Poll Interval (s)", &poll_sec, 15, 300)) {
        config.SetState("dm.poll_interval", std::to_string(poll_sec));
    }
}

} // namespace YipOS
