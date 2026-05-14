#include "YCScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "net/BridgeClient.hpp"
#include "core/Glyphs.hpp"
#include <cstring>

namespace YipOS {

using namespace Glyphs;

YCScreen::YCScreen(PDAController& pda) : Screen(pda) {
    name = "YIPAI";
    macro_index = 48;
    update_interval = 0.5f;

    desired_.fill(' ');
    displayed_.fill(' ');
}

void YCScreen::WordWrap(const std::string& text, int start_row, int max_rows, bool invert) {
    int row = start_row, col = 0;
    size_t i = 0;
    while (i < text.size() && row < start_row + max_rows) {
        if (text[i] == '\n') {
            row++;
            col = 0;
            i++;
            continue;
        }

        size_t word_start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\n') i++;
        int word_len = static_cast<int>(i - word_start);

        if (col > 0 && col + word_len > TEXT_COLS) {
            row++;
            col = 0;
            if (row >= start_row + max_rows) break;
        }

        for (size_t j = word_start; j < i && row < start_row + max_rows; j++) {
            int ch = static_cast<int>(text[j]);
            if (ch < 32 || ch > 126) ch = '?';
            if (invert) ch += INVERT_OFFSET;
            if (col < TEXT_COLS) {
                desired_[row * TEXT_COLS + col] = ch;
                col++;
            } else {
                row++;
                col = 0;
                if (row >= start_row + max_rows) break;
                desired_[row * TEXT_COLS + col] = ch;
                col++;
            }
        }

        if (i < text.size() && text[i] == ' ') {
            if (col < TEXT_COLS) col++;
            i++;
        }
    }
}

void YCScreen::ComputeCells() {
    desired_.fill(' ');

    auto* bridge = pda_.GetBridgeClient();
    if (!bridge || !bridge->IsConnected()) {
        const char* line1 = "Not connected";
        const char* line2 = "Waiting for YipAI...";
        int r1 = 2 * TEXT_COLS;
        int r2 = 3 * TEXT_COLS;
        for (int i = 0; line1[i]; i++) desired_[r1 + i + 1] = line1[i];
        for (int i = 0; line2[i]; i++) desired_[r2 + i + 1] = line2[i];
        return;
    }

    auto data = bridge->GetData();

    // Row 0: expression + thinking indicator
    if (!data.expression.empty()) {
        std::string expr_str = "[" + data.expression + "]";
        for (int i = 0; i < static_cast<int>(expr_str.size()) && i < TEXT_COLS; i++)
            desired_[i] = expr_str[i];
    }
    if (data.thinking) {
        const char* dots[] = {".", "..", "..."};
        const char* anim = dots[thinking_frame_ % 3];
        int start = TEXT_COLS - static_cast<int>(strlen(anim));
        for (int i = 0; anim[i]; i++)
            desired_[start + i] = anim[i];
    }

    // Rows 1-6: utterance (default) or thoughts (SEL toggle, inverted)
    if (show_thoughts_) {
        if (!data.thought.empty()) {
            std::string thought_str = "(" + data.thought + ")";
            WordWrap(thought_str, 1, TEXT_ROWS - 1, true);
        }
    } else {
        if (!data.display_text.empty()) {
            WordWrap(data.display_text, 1, TEXT_ROWS - 1, false);
        }
    }
}

void YCScreen::FlushDiff() {
    display_.CancelBuffered();
    display_.BeginBuffered();
    for (int idx = 0; idx < TOTAL_CELLS; idx++) {
        if (desired_[idx] != displayed_[idx]) {
            int r = idx / TEXT_COLS;
            int c = idx % TEXT_COLS;
            display_.WriteChar(1 + c, 1 + r, desired_[idx]);
            displayed_[idx] = desired_[idx];
        }
    }
}

// Used when the active text channel's content changes: flash the screen by
// stamping the YC frame macro (instant full-screen redraw), then queue only
// the non-space cells of the new content. Avoids the slow per-cell clear that
// happens when diffing in slow-write mode.
void YCScreen::FlashAndDraw() {
    display_.CancelBuffered();
    display_.SetMacroMode();
    display_.StampMacro(macro_index);
    display_.SetTextMode();
    display_.BeginBuffered();

    // The stamp wiped the screen back to the macro (frame only), so all text
    // cells are effectively blank now.
    displayed_.fill(' ');

    for (int idx = 0; idx < TOTAL_CELLS; idx++) {
        if (desired_[idx] != ' ') {
            int r = idx / TEXT_COLS;
            int c = idx % TEXT_COLS;
            display_.WriteChar(1 + c, 1 + r, desired_[idx]);
            displayed_[idx] = desired_[idx];
        }
    }

    RenderStatusBar();
}

void YCScreen::FlushRefresh() {
    display_.CancelBuffered();
    display_.BeginBuffered();
    for (int n = 0; n < REFRESH_CHUNK; n++) {
        int idx = refresh_pos_;
        int r = idx / TEXT_COLS;
        int c = idx % TEXT_COLS;
        display_.WriteChar(1 + c, 1 + r, desired_[idx]);
        displayed_[idx] = desired_[idx];
        refresh_pos_ = (refresh_pos_ + 1) % TOTAL_CELLS;
    }
}

void YCScreen::Render() {
    auto* bridge = pda_.GetBridgeClient();
    std::string title = "YipAI";
    if (bridge && bridge->IsConnected()) {
        auto data = bridge->GetData();
        if (!data.companion_name.empty())
            title = "YipAI: " + data.companion_name;
    }
    RenderFrame(title);

    ComputeCells();
    for (int idx = 0; idx < TOTAL_CELLS; idx++) {
        if (desired_[idx] != ' ') {
            int r = idx / TEXT_COLS;
            int c = idx % TEXT_COLS;
            display_.WriteChar(1 + c, 1 + r, desired_[idx]);
        }
        displayed_[idx] = desired_[idx];
    }

    RenderStatusBar();
}

void YCScreen::RenderDynamic() {
    RenderClock();
    RenderCursor();
}

void YCScreen::Update() {
    thinking_frame_++;

    auto* bridge = pda_.GetBridgeClient();
    bool connected = (bridge && bridge->IsConnected());
    CompanionData data;
    if (bridge) data = bridge->GetData();

    // Only the active text channel matters for redraw.
    const std::string& active_text = show_thoughts_ ? data.thought : data.display_text;
    const std::string& last_active = show_thoughts_ ? last_thought_ : last_display_text_;

    bool active_text_changed = (active_text != last_active);
    bool toggle_changed = (show_thoughts_ != last_show_thoughts_);
    bool connection_changed = (connected != last_connected_);

    bool changed = (active_text_changed ||
                    data.expression != last_expression_ ||
                    data.thinking != last_thinking_ ||
                    toggle_changed ||
                    connection_changed);

    if (!changed) {
        FlushRefresh();
        return;
    }

    ComputeCells();
    last_display_text_ = data.display_text;
    last_expression_ = data.expression;
    last_thought_ = data.thought;
    last_thinking_ = data.thinking;
    last_show_thoughts_ = show_thoughts_;
    last_connected_ = connected;

    // New utterance/thought, channel toggle, or (re)connect: flash the screen
    // via macro stamp, then draw — instant clear instead of per-cell erase.
    if (active_text_changed || toggle_changed || connection_changed) {
        FlashAndDraw();
    } else {
        FlushDiff();
    }
}

bool YCScreen::OnInput(const std::string& key) {
    if (key == "TR") {
        show_thoughts_ = !show_thoughts_;
        return true;
    }
    return false;
}

} // namespace YipOS
