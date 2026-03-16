#include "CCScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "audio/WhisperWorker.hpp"
#include "core/Logger.hpp"

namespace YipOS {

using namespace Glyphs;

CCScreen::CCScreen(PDAController& pda) : Screen(pda) {
    name = "CC";
    macro_index = 14;
}

void CCScreen::Render() {
    RenderFrame("CC");
    RenderLines();

    // Status bar with CONF button (touch 53) instead of clock
    display_.WriteGlyph(0, 7, G_BL_CORNER);
    RenderCursor();
    for (int c = 2; c < COLS - 5; c++) {
        display_.WriteGlyph(c, 7, G_HLINE);
    }
    WriteInverted(COLS - 5, 7, "CONF");
    display_.WriteGlyph(COLS - 1, 7, G_BR_CORNER);
}

void CCScreen::RenderDynamic() {
    RenderLines();
    RenderCursor();
}

void CCScreen::Update() {
    auto* whisper = pda_.GetWhisperWorker();
    if (!whisper) return;

    // Pull all available text from the whisper worker
    while (whisper->HasText()) {
        std::string text = whisper->PopText();
        if (!text.empty()) {
            WrapAndAppend(text);
        }
    }
}

void CCScreen::RenderLines() {
    auto& d = display_;

    auto* whisper = pda_.GetWhisperWorker();
    if (!whisper || !whisper->IsModelLoaded()) {
        d.WriteText(2, 3, "No model loaded");
        d.WriteText(2, 4, "Place ggml-tiny.en.bin in");
        std::string path = WhisperWorker::DefaultModelPath("tiny.en");
        // Truncate path to fit
        if (static_cast<int>(path.size()) > LINE_WIDTH)
            path = "..." + path.substr(path.size() - LINE_WIDTH + 3);
        d.WriteText(2, 5, path);
        return;
    }

    if (!whisper->IsRunning()) {
        d.WriteText(2, 3, "CC inactive");
        d.WriteText(2, 4, "Enable in CC CONF");
        return;
    }

    if (lines_.empty()) {
        d.WriteText(2, 3, "Listening...");
        return;
    }

    // Show the last VISIBLE_ROWS lines
    int total = static_cast<int>(lines_.size());
    int start = (total > VISIBLE_ROWS) ? total - VISIBLE_ROWS : 0;
    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = start + i;
        int row = 1 + i;
        if (idx < total) {
            d.WriteText(1, row, lines_[idx]);
        }
    }
}

void CCScreen::WriteInverted(int col, int row, const std::string& text) {
    for (int i = 0; i < static_cast<int>(text.size()); i++) {
        int ch = static_cast<int>(text[i]) + INVERT_OFFSET;
        display_.WriteChar(col + i, row, ch);
    }
}

void CCScreen::WrapAndAppend(const std::string& text) {
    // Word-wrap text into LINE_WIDTH-character lines
    size_t pos = 0;
    while (pos < text.size()) {
        size_t remaining = text.size() - pos;
        if (static_cast<int>(remaining) <= LINE_WIDTH) {
            lines_.push_back(text.substr(pos));
            break;
        }

        // Find last space within LINE_WIDTH
        size_t end = pos + LINE_WIDTH;
        size_t break_at = text.rfind(' ', end);
        if (break_at == std::string::npos || break_at <= pos) {
            // No space found, hard break
            break_at = end;
        }

        lines_.push_back(text.substr(pos, break_at - pos));
        pos = break_at;
        if (pos < text.size() && text[pos] == ' ') pos++; // skip space
    }

    // Keep buffer bounded
    while (lines_.size() > 200)
        lines_.erase(lines_.begin());
}

bool CCScreen::OnInput(const std::string& key) {
    // Touch 53 → CONF button
    if (key == "53") {
        pda_.SetPendingNavigate("CC_CONF");
        return true;
    }
    return false;
}

} // namespace YipOS
