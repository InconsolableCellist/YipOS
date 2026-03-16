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

    // Row 5: Window size
    if (whisper) {
        char wbuf[32];
        std::snprintf(wbuf, sizeof(wbuf), "Window: %ds", whisper->GetChunkSeconds());
        d.WriteText(1, 5, wbuf);
    }

    // Row 6: Info
    d.WriteText(1, 6, "Config in desktop app");
}

bool CCConfScreen::OnInput(const std::string& key) {
    // No interactive buttons — config is managed from the desktop UI
    return false;
}

} // namespace YipOS
