#include "CCScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "audio/AudioCapture.hpp"
#include "audio/WhisperWorker.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"

namespace YipOS {

using namespace Glyphs;

CCScreen::CCScreen(PDAController& pda) : Screen(pda) {
    name = "CC";
    macro_index = 14;
    update_interval = 0.5f;
    skip_clock = true; // we own the full display, no clock writes

    // Auto-start CC on screen entry
    StartCC();
}

CCScreen::~CCScreen() {
    // Auto-stop if we started it
    if (started_by_screen_) {
        StopCC();
    }
}

void CCScreen::StartCC() {
    auto* whisper = pda_.GetWhisperWorker();
    auto* audio = pda_.GetAudioCapture();
    if (!whisper || !audio) return;

    // Already running? Don't restart
    if (whisper->IsRunning()) return;

    // Load saved model from NVRAM
    if (!whisper->IsModelLoaded()) {
        std::string saved_model = pda_.GetConfig().GetState("cc.model", "tiny.en");
        std::string path = WhisperWorker::DefaultModelPath(saved_model);
        if (!whisper->LoadModel(path)) {
            // Try fallback
            path = WhisperWorker::DefaultModelPath("tiny.en");
            whisper->LoadModel(path);
        }
    }

    if (!whisper->IsModelLoaded()) {
        Logger::Warning("CC: No model available for auto-start");
        return;
    }

    // Restore saved chunk window
    std::string saved_window = pda_.GetConfig().GetState("cc.window");
    if (!saved_window.empty()) {
        whisper->SetChunkSeconds(std::stoi(saved_window));
    }

    // Restore saved audio device
    std::string saved_device = pda_.GetConfig().GetState("cc.device");
    if (!saved_device.empty()) {
        audio->SetDevice(saved_device);
    }

    audio->Start();
    whisper->Start(audio->GetBuffer());
    started_by_screen_ = true;

    // Save current settings to NVRAM
    pda_.GetConfig().SetState("cc.model", whisper->GetModelName());
    std::string dev_id = audio->GetCurrentDeviceId();
    if (!dev_id.empty()) {
        pda_.GetConfig().SetState("cc.device", dev_id);
    }

    Logger::Info("CC: Auto-started");
}

void CCScreen::StopCC() {
    auto* whisper = pda_.GetWhisperWorker();
    auto* audio = pda_.GetAudioCapture();
    if (whisper && whisper->IsRunning()) whisper->Stop();
    if (audio && audio->IsRunning()) audio->Stop();
    started_by_screen_ = false;
    Logger::Info("CC: Auto-stopped");
}

bool CCScreen::FilterText(const std::string& text) const {
    if (text.empty()) return true;
    // Whisper junk — these are now also filtered at the source (WhisperWorker),
    // but double-check here in case any slip through
    if (text.find("[BLANK_AUDIO]") != std::string::npos) return true;
    if (text.find("[SILENCE]") != std::string::npos) return true;
    if (text.find("[ Silence ]") != std::string::npos) return true;
    if (text.find("(silence)") != std::string::npos) return true;
    if (text.find("[Music]") != std::string::npos) return true;
    bool has_alpha = false;
    for (char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            has_alpha = true;
            break;
        }
    }
    if (!has_alpha) return true;
    return false;
}

void CCScreen::Render() {
    RenderFrame("CC");

    auto* whisper = pda_.GetWhisperWorker();
    if (!whisper || !whisper->IsModelLoaded()) {
        display_.WriteText(2, 2, "No model loaded");
        display_.WriteText(2, 3, "Place ggml-tiny.en.bin");
        display_.WriteText(2, 4, "in config/models/");
    } else if (!whisper->IsRunning()) {
        display_.WriteText(2, 3, "Starting...");
    } else {
        display_.WriteText(2, 3, "Listening...");
    }

    // CONF button on row 6 (touch 53 area)
    WriteInverted(COLS - 1 - 4, 6, "CONF");

    display_.WriteGlyph(0, 7, G_BL_CORNER);
    for (int c = 1; c < COLS - 1; c++) {
        display_.WriteGlyph(c, 7, G_HLINE);
    }
    display_.WriteGlyph(COLS - 1, 7, G_BR_CORNER);
}

void CCScreen::RenderDynamic() {
    // No-op: text is written incrementally via Update()
}

void CCScreen::Update() {
    auto* whisper = pda_.GetWhisperWorker();
    if (!whisper) return;

    // Pull new text from whisper, word-wrap into lines
    while (whisper->HasText()) {
        std::string text = whisper->PopText();
        if (FilterText(text)) continue;

        // Word-wrap into LINE_WIDTH lines
        size_t pos = 0;
        while (pos < text.size()) {
            size_t remaining = text.size() - pos;
            if (static_cast<int>(remaining) <= LINE_WIDTH) {
                pending_lines_.push_back(text.substr(pos));
                break;
            }
            size_t end = pos + LINE_WIDTH;
            size_t break_at = text.rfind(' ', end);
            if (break_at == std::string::npos || break_at <= pos) {
                break_at = end;
            }
            pending_lines_.push_back(text.substr(pos, break_at - pos));
            pos = break_at;
            if (pos < text.size() && text[pos] == ' ') pos++;
        }

        // Drop oldest lines if too many pending (keep newest)
        while (pending_lines_.size() > MAX_PENDING_LINES) {
            pending_lines_.erase(pending_lines_.begin());
            line_char_pos_ = 0;
        }
    }

    // Nothing to write
    if (pending_lines_.empty()) return;

    // Write one line per update tick (buffered)
    display_.BeginBuffered();

    const std::string& line = pending_lines_.front();

    // If starting a new line, clear the row first
    if (line_char_pos_ == 0) {
        for (int c = LEFT_COL; c <= RIGHT_COL; c++) {
            display_.WriteChar(c, cursor_row_, 32);
        }
    }

    // Write remaining chars of this line
    while (line_char_pos_ < static_cast<int>(line.size())) {
        char ch = line[line_char_pos_];
        int char_idx = (ch >= 32 && ch <= 126) ? static_cast<int>(ch) : 32;
        display_.WriteChar(LEFT_COL + line_char_pos_, cursor_row_, char_idx);
        line_char_pos_++;
    }

    // Line done — advance
    pending_lines_.erase(pending_lines_.begin());
    line_char_pos_ = 0;
    cursor_row_++;
    if (cursor_row_ > LAST_ROW) {
        cursor_row_ = FIRST_ROW;
    }
}

void CCScreen::WriteInverted(int col, int row, const std::string& text) {
    for (int i = 0; i < static_cast<int>(text.size()); i++) {
        int ch = static_cast<int>(text[i]) + INVERT_OFFSET;
        display_.WriteChar(col + i, row, ch);
    }
}

bool CCScreen::OnInput(const std::string& key) {
    if (key == "52" || key == "53") {
        pda_.SetPendingNavigate("CC_CONF");
        return true;
    }
    return false;
}

} // namespace YipOS
