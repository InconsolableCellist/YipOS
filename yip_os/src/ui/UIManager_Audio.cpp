#include "UIManager.hpp"
#include "app/PDAController.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "audio/AudioCapture.hpp"
#include "audio/WhisperWorker.hpp"

#include <imgui.h>
#include <cstdio>
#include <string>
#include <vector>

namespace YipOS {

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

    // Processing config
    ImGui::Text("Processing");

    int step_ms = whisper->GetStepMs();
    if (ImGui::SliderInt("Step (ms)", &step_ms, 2000, 5000)) {
        whisper->SetStepMs(step_ms);
        config.SetState("cc.step", std::to_string(step_ms));
    }
    ImGui::TextDisabled("Inference interval. Lower = faster updates, higher = more efficient.");

    int length_ms = whisper->GetLengthMs();
    if (ImGui::SliderInt("Window (ms)", &length_ms, 5000, 15000)) {
        whisper->SetLengthMs(length_ms);
        config.SetState("cc.window", std::to_string(length_ms));
    }
    ImGui::TextDisabled("Audio context window. Longer = better accuracy, more compute.");

    ImGui::Spacing();

    // Translation support
    if (whisper->IsModelLoaded()) {
        if (whisper->SupportsTranslation()) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Translation: enabled");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                "Translation: disabled (turbo/distil/en-only models)");
        }
    }

    ImGui::TextDisabled("Progressive text preview requires ULTRA write speed.");
    ImGui::TextDisabled("At NORM/SLOW, only finalized text is displayed.");

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

} // namespace YipOS
