#include "CCConfScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "audio/AudioCapture.hpp"
#include "audio/WhisperWorker.hpp"
#include "core/Logger.hpp"

namespace YipOS {

using namespace Glyphs;

CCConfScreen::CCConfScreen(PDAController& pda) : Screen(pda) {
    name = "CC CONF";
    macro_index = 15;
}

void CCConfScreen::Render() {
    RenderFrame("CC CONF");
    RenderContent();
    RenderStatusBar();
}

void CCConfScreen::RenderDynamic() {
    RenderContent();
    RenderClock();
    RenderCursor();
}

void CCConfScreen::WriteInverted(int col, int row, const std::string& text) {
    for (int i = 0; i < static_cast<int>(text.size()); i++) {
        int ch = static_cast<int>(text[i]) + INVERT_OFFSET;
        display_.WriteChar(col + i, row, ch);
    }
}

void CCConfScreen::FlashButton(int col, int row, const std::string& text) {
    for (int i = 0; i < static_cast<int>(text.size()); i++)
        display_.WriteChar(col + i, row, static_cast<int>(text[i]));
    for (int i = 0; i < static_cast<int>(text.size()); i++)
        display_.WriteChar(col + i, row, static_cast<int>(text[i]) + INVERT_OFFSET);
}

void CCConfScreen::RenderContent() {
    auto& d = display_;
    int max_w = COLS - 2;

    auto* whisper = pda_.GetWhisperWorker();
    auto* audio = pda_.GetAudioCapture();

    // Row 1: Model
    std::string model = "Model: ";
    if (whisper && whisper->IsModelLoaded())
        model += whisper->GetModelName();
    else
        model += "(none)";
    if (static_cast<int>(model.size()) > max_w)
        model = model.substr(0, max_w);
    d.WriteText(1, 1, model);

    // Row 2: Audio device
    std::string dev = "Dev: ";
    if (audio)
        dev += audio->GetCurrentDeviceName();
    else
        dev += "(none)";
    if (static_cast<int>(dev.size()) > max_w)
        dev = dev.substr(0, max_w);
    d.WriteText(1, 2, dev);

    // Row 3: Status
    std::string status = "Status: ";
    if (whisper && whisper->IsRunning())
        status += "LISTENING";
    else if (whisper && whisper->IsModelLoaded())
        status += "READY";
    else
        status += "NO MODEL";
    d.WriteText(1, 3, status);

    // Row 4: Audio status
    std::string astatus = "Audio: ";
    if (audio && audio->IsRunning())
        astatus += "CAPTURING";
    else
        astatus += "STOPPED";
    d.WriteText(1, 4, astatus);

    // Row 5-6: START/STOP button (touch 53)
    bool active = (whisper && whisper->IsRunning());
    std::string btn = active ? "STOP" : "START";
    WriteInverted(COLS - 1 - static_cast<int>(btn.size()), 5, btn);
    std::string btn2 = "(TOGGLE)";
    WriteInverted(COLS - 1 - static_cast<int>(btn2.size()), 6, btn2);
}

bool CCConfScreen::OnInput(const std::string& key) {
    // Touch 53 → START/STOP toggle
    if (key == "53") {
        auto* whisper = pda_.GetWhisperWorker();
        auto* audio = pda_.GetAudioCapture();
        if (!whisper || !audio) return true;

        display_.CancelBuffered();
        display_.BeginBuffered();

        bool active = whisper->IsRunning();
        std::string btn = active ? "STOP" : "START";
        std::string btn2 = "(TOGGLE)";
        FlashButton(COLS - 1 - static_cast<int>(btn.size()), 5, btn);
        FlashButton(COLS - 1 - static_cast<int>(btn2.size()), 6, btn2);

        if (active) {
            whisper->Stop();
            audio->Stop();
            Logger::Info("CC: Stopped");
        } else {
            // Load model if not loaded
            if (!whisper->IsModelLoaded()) {
                std::string path = WhisperWorker::DefaultModelPath("tiny.en");
                if (!whisper->LoadModel(path)) {
                    // Try base.en
                    path = WhisperWorker::DefaultModelPath("base.en");
                    whisper->LoadModel(path);
                }
            }

            if (whisper->IsModelLoaded()) {
                audio->Start();
                whisper->Start(audio->GetBuffer());
                Logger::Info("CC: Started");
            } else {
                Logger::Warning("CC: No model available");
            }
        }

        pda_.StartRender(this);
        return true;
    }

    return false;
}

} // namespace YipOS
