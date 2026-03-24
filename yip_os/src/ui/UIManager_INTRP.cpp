#include "UIManager.hpp"
#include "app/PDAController.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "audio/AudioCapture.hpp"
#include "audio/WhisperWorker.hpp"
#ifdef YIPOS_HAS_TRANSLATION
#include "translate/TranslationWorker.hpp"
#endif

#include <imgui.h>
#include <string>
#include <vector>

namespace YipOS {

// Language codes and display names
static const struct { const char* code; const char* name; } INTRP_LANGS[] = {
    {"en", "English"},
    {"es", "Español"},
    {"fr", "Français"},
    {"de", "Deutsch"},
    {"it", "Italiano"},
    {"ja", "Japanese (kana)"},
    {"pt", "Português"},
};
static constexpr int INTRP_LANG_COUNT = sizeof(INTRP_LANGS) / sizeof(INTRP_LANGS[0]);

static int FindLangIndex(const std::string& code) {
    for (int i = 0; i < INTRP_LANG_COUNT; i++)
        if (code == INTRP_LANGS[i].code) return i;
    return 0;
}

void UIManager::RenderINTRPTab(PDAController& pda, Config& config) {
    ImGui::Text("Interpreter (INTRP)");
    ImGui::TextDisabled("Real-time bilingual conversation tool. Captures two audio");
    ImGui::TextDisabled("streams simultaneously: your mic and their VRChat voice via");
    ImGui::TextDisabled("system audio loopback. Each stream runs its own Whisper instance.");

    ImGui::Separator();

    // ---- Language configuration ----
    ImGui::Text("Languages");
    ImGui::TextDisabled("Configure which language each speaker uses.");

    {
        std::string my_code = config.GetState("intrp.my_lang", "en");
        int my_idx = FindLangIndex(my_code);
        if (ImGui::BeginCombo("I speak##intrp_my", INTRP_LANGS[my_idx].name)) {
            for (int i = 0; i < INTRP_LANG_COUNT; i++) {
                bool selected = (i == my_idx);
                if (ImGui::Selectable(INTRP_LANGS[i].name, selected)) {
                    config.SetState("intrp.my_lang", INTRP_LANGS[i].code);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        std::string their_code = config.GetState("intrp.their_lang", "es");
        int their_idx = FindLangIndex(their_code);
        if (ImGui::BeginCombo("They speak##intrp_their", INTRP_LANGS[their_idx].name)) {
            for (int i = 0; i < INTRP_LANG_COUNT; i++) {
                bool selected = (i == their_idx);
                if (ImGui::Selectable(INTRP_LANGS[i].name, selected)) {
                    config.SetState("intrp.their_lang", INTRP_LANGS[i].code);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Separator();

    // ---- Whisper model (shared with CC) ----
    auto* whisper_mic = pda.GetWhisperWorker();
    auto* whisper_loop = pda.GetWhisperWorkerLoopback();

    ImGui::Text("Whisper Model");
    ImGui::TextDisabled("Both streams use the same model. For INTRP you MUST use a");
    ImGui::TextDisabled("multilingual model (no .en suffix). The .en models only do English.");
    ImGui::Spacing();
    ImGui::TextDisabled("Recommended models (download ggml-*.bin files):");
    ImGui::BulletText("ggml-base.bin   (148 MB) - fast, good for casual conversation");
    ImGui::BulletText("ggml-small.bin  (488 MB) - better accuracy, moderate speed");
    ImGui::BulletText("ggml-medium.bin (1.5 GB) - best accuracy, slower");
    ImGui::Spacing();
    ImGui::TextDisabled("Download from: huggingface.co/ggerganov/whisper.cpp/tree/main");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Direct download links (copy to browser):");
        ImGui::Spacing();
        ImGui::TextWrapped("huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin");
        ImGui::TextWrapped("huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin");
        ImGui::TextWrapped("huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin");
        ImGui::Spacing();
#ifdef _WIN32
        ImGui::Text("Place in: %%APPDATA%%\\yip_os\\models\\");
#else
        ImGui::Text("Place in: ~/.config/yip_os/models/");
#endif
        ImGui::TextDisabled("Do NOT use tiny.en, base.en, etc. — those are English-only.");
        ImGui::EndTooltip();
    }
    ImGui::Spacing();

    if (whisper_mic && whisper_mic->IsModelLoaded()) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Mic model: %s",
                           whisper_mic->GetModelName().c_str());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Mic model: not loaded");
    }
    if (whisper_loop && whisper_loop->IsModelLoaded()) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Loopback model: %s",
                           whisper_loop->GetModelName().c_str());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Loopback model: not loaded");
    }

    {
        static std::vector<std::string> models;
        static bool scanned = false;
        if (!scanned) {
            models = WhisperWorker::ScanAvailableModels();
            scanned = true;
        }
        if (!models.empty()) {
            std::string saved = config.GetState("intrp.model",
                                                config.GetState("cc.model", ""));
            std::string preview = saved.empty() ? "(select model)" : saved;
            if (ImGui::BeginCombo("Model##intrp_model", preview.c_str())) {
                for (auto& m : models) {
                    bool sel = (m == saved);
                    if (ImGui::Selectable(m.c_str(), sel)) {
                        config.SetState("intrp.model", m);
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "No models found");
        }
        if (ImGui::Button("Rescan Models##intrp")) {
            models = WhisperWorker::ScanAvailableModels();
        }
    }

    ImGui::Separator();

    // ---- Audio device: YOUR mic ----
    auto* audio_mic = pda.GetAudioCapture();
    ImGui::Text("Your Audio Device (Mic)");
    ImGui::TextDisabled("Select the microphone that captures YOUR voice.");

    if (audio_mic) {
        static std::vector<AudioDevice> mic_devices;
        static int mic_idx = -1;
        static bool mic_init = false;

        if (!mic_init) {
            mic_devices = audio_mic->GetDevices();
            std::string saved = config.GetState("intrp.mic_device");
            if (!saved.empty()) {
                for (int i = 0; i < static_cast<int>(mic_devices.size()); i++) {
                    if (mic_devices[i].id == saved) {
                        mic_idx = i;
                        audio_mic->SetDevice(saved);
                        break;
                    }
                }
            }
            mic_init = true;
        }

        if (ImGui::Button("Refresh##intrp_mic")) {
            mic_devices = audio_mic->GetDevices();
            std::string cur = audio_mic->GetCurrentDeviceId();
            mic_idx = -1;
            for (int i = 0; i < static_cast<int>(mic_devices.size()); i++) {
                if (mic_devices[i].id == cur) { mic_idx = i; break; }
            }
        }
        if (!mic_devices.empty()) {
            std::string preview;
            if (mic_idx >= 0 && mic_idx < static_cast<int>(mic_devices.size()))
                preview = mic_devices[mic_idx].name;
            else
                preview = audio_mic->GetCurrentDeviceName();
            if (ImGui::BeginCombo("##intrp_mic_dev", preview.c_str())) {
                for (int i = 0; i < static_cast<int>(mic_devices.size()); i++) {
                    bool sel = (i == mic_idx);
                    if (ImGui::Selectable(mic_devices[i].name.c_str(), sel)) {
                        mic_idx = i;
                        audio_mic->SetDevice(mic_devices[i].id);
                        config.SetState("intrp.mic_device", mic_devices[i].id);
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextDisabled("No devices found. Click Refresh.");
        }
        ImGui::Text("Current: %s", audio_mic->GetCurrentDeviceName().c_str());
    }

    ImGui::Separator();

    // ---- Audio device: THEIR voice (loopback) ----
    auto* audio_loop = pda.GetAudioCaptureLoopback();
    ImGui::Text("Their Audio Device (System Loopback)");
    ImGui::TextDisabled("Select the system audio loopback that captures their VRChat voice.");
    ImGui::TextDisabled("On Linux, look for 'Monitor of <output device>'. On Windows,");
    ImGui::TextDisabled("look for 'Stereo Mix' or a WASAPI loopback device.");

    if (audio_loop) {
        static std::vector<AudioDevice> loop_devices;
        static int loop_idx = -1;
        static bool loop_init = false;

        if (!loop_init) {
            loop_devices = audio_loop->GetDevices();
            std::string saved = config.GetState("intrp.loop_device");
            if (!saved.empty()) {
                for (int i = 0; i < static_cast<int>(loop_devices.size()); i++) {
                    if (loop_devices[i].id == saved) {
                        loop_idx = i;
                        audio_loop->SetDevice(saved);
                        break;
                    }
                }
            }
            loop_init = true;
        }

        if (ImGui::Button("Refresh##intrp_loop")) {
            loop_devices = audio_loop->GetDevices();
            std::string cur = audio_loop->GetCurrentDeviceId();
            loop_idx = -1;
            for (int i = 0; i < static_cast<int>(loop_devices.size()); i++) {
                if (loop_devices[i].id == cur) { loop_idx = i; break; }
            }
        }
        if (!loop_devices.empty()) {
            std::string preview;
            if (loop_idx >= 0 && loop_idx < static_cast<int>(loop_devices.size()))
                preview = loop_devices[loop_idx].name;
            else
                preview = audio_loop->GetCurrentDeviceName();
            if (ImGui::BeginCombo("##intrp_loop_dev", preview.c_str())) {
                for (int i = 0; i < static_cast<int>(loop_devices.size()); i++) {
                    bool sel = (i == loop_idx);
                    if (ImGui::Selectable(loop_devices[i].name.c_str(), sel)) {
                        loop_idx = i;
                        audio_loop->SetDevice(loop_devices[i].id);
                        config.SetState("intrp.loop_device", loop_devices[i].id);
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextDisabled("No devices found. Click Refresh.");
        }
        ImGui::Text("Current: %s", audio_loop->GetCurrentDeviceName().c_str());
    }

    ImGui::Separator();

    // ---- Translation model (NLLB) ----
#ifdef YIPOS_HAS_TRANSLATION
    ImGui::Text("Translation Model (NLLB)");
    ImGui::TextDisabled("Offline neural machine translation via CTranslate2 + NLLB-200.");
    ImGui::TextDisabled("Without this model, INTRP shows raw speech-to-text (no translation).");
    ImGui::Spacing();
    ImGui::TextDisabled("Download a pre-converted model (recommended):");
    ImGui::BulletText("600M (623 MB) - fast, good for casual use");
    ImGui::BulletText("1.3B (1.4 GB) - better accuracy, slower");
    ImGui::Spacing();
    ImGui::TextDisabled("Download with huggingface-cli or browser:");
    ImGui::BulletText("huggingface.co/JustFrederik/nllb-200-distilled-600M-ct2-int8");
    ImGui::BulletText("huggingface.co/JustFrederik/nllb-200-distilled-1.3B-ct2-int8");
    ImGui::Spacing();
    ImGui::TextDisabled("Place all files (model.bin + sentencepiece.bpe.model + ...) in:");
#ifdef _WIN32
    ImGui::TextDisabled("  %%APPDATA%%\\yip_os\\models\\nllb\\");
#else
    ImGui::TextDisabled("  ~/.config/yip_os/models/nllb/");
#endif
    ImGui::Spacing();
    ImGui::TextDisabled("Quick setup:");
    ImGui::TextDisabled("  pip install huggingface-hub");
#ifdef _WIN32
    ImGui::TextDisabled("  huggingface-cli download JustFrederik/nllb-200-distilled-600M-ct2-int8");
    ImGui::TextDisabled("      --local-dir %%APPDATA%%\\yip_os\\models\\nllb");
#else
    ImGui::TextDisabled("  huggingface-cli download JustFrederik/nllb-200-distilled-600M-ct2-int8");
    ImGui::TextDisabled("      --local-dir ~/.config/yip_os/models/nllb");
#endif
    ImGui::Spacing();

    auto* translator = pda.GetTranslationWorker();
    if (translator && translator->IsModelLoaded()) {
        if (translator->IsRunning()) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "NLLB: loaded and running (%s)",
                               translator->GetDeviceName().c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "NLLB: loaded (not started — navigate to INTRP)");
        }
    } else {
        std::string nllb_path = TranslationWorker::DefaultModelPath();
        if (TranslationWorker::ModelExists(nllb_path)) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "NLLB: model found but not loaded yet");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "NLLB: model not found");
        }
    }
#else
    ImGui::Text("Translation");
    ImGui::TextDisabled("Built without CTranslate2 — translation is disabled.");
    ImGui::TextDisabled("INTRP will show raw speech-to-text without translation.");
#endif

    ImGui::Separator();

    // ---- Status ----
    ImGui::Text("Status");
    bool mic_running = whisper_mic && whisper_mic->IsRunning();
    bool loop_running = whisper_loop && whisper_loop->IsRunning();

    if (mic_running && loop_running) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Both streams: LISTENING");
    } else if (mic_running) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Mic: LISTENING | Loopback: stopped");
    } else if (loop_running) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Mic: stopped | Loopback: LISTENING");
    } else {
        ImGui::Text("Status: idle (navigate to INTRP screen to start)");
    }

    if (mic_running || loop_running) {
        if (ImGui::Button("Force Stop INTRP")) {
            if (whisper_mic) whisper_mic->Stop();
            if (audio_mic) audio_mic->Stop();
            if (whisper_loop) whisper_loop->Stop();
            if (audio_loop) audio_loop->Stop();
        }
        ImGui::TextDisabled("INTRP normally stops automatically when you leave the screen.");
    }

    // Latest text preview
    ImGui::Separator();
    ImGui::Text("Preview");
    if (whisper_mic) {
        std::string latest = whisper_mic->PeekLatest();
        if (!latest.empty()) {
            ImGui::TextWrapped("You (speech): %s", latest.c_str());
        }
    }
    if (whisper_loop) {
        std::string latest = whisper_loop->PeekLatest();
        if (!latest.empty()) {
            ImGui::TextWrapped("They (speech): %s", latest.c_str());
        }
    }
#ifdef YIPOS_HAS_TRANSLATION
    if (translator && translator->IsRunning()) {
        std::string t0 = translator->PeekLatestTranslated(0);
        std::string t1 = translator->PeekLatestTranslated(1);
        if (!t0.empty()) {
            ImGui::TextWrapped("They (translated): %s", t0.c_str());
        }
        if (!t1.empty()) {
            ImGui::TextWrapped("You (translated): %s", t1.c_str());
        }
    }
#endif
}

} // namespace YipOS
