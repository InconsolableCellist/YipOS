#include "PDADisplay.hpp"
#include "net/OSCManager.hpp"
#include "core/Logger.hpp"
#include <cmath>
#include <thread>
#include <chrono>

namespace YipOS {

using namespace Glyphs;

PDADisplay::PDADisplay(OSCManager& osc, ScreenBuffer& screen,
                       float y_offset, float y_scale, float y_curve,
                       float write_delay, float settle_delay)
    : osc_(osc), screen_(screen),
      y_offset_(y_offset), y_scale_(y_scale), y_curve_(y_curve),
      write_delay_(write_delay), settle_delay_(settle_delay) {}

void PDADisplay::SleepMs(float seconds) {
    if (seconds > 0.0f) {
        std::this_thread::sleep_for(
            std::chrono::microseconds(static_cast<int>(seconds * 1000000.0f)));
    }
}

float PDADisplay::RowToY(float row) const {
    float t = (row + 0.5f) / static_cast<float>(ROWS);
    return y_offset_ + std::pow(t, y_curve_) * y_scale_;
}

void PDADisplay::SendParam(const std::string& name, float value) {
    osc_.SendFloat(std::string(PARAM_PREFIX) + name, value);
}

void PDADisplay::SendParam(const std::string& name, bool value) {
    osc_.SendBool(std::string(PARAM_PREFIX) + name, value);
}

void PDADisplay::MoveCursor(int col, float row) {
    float nx = (col + 0.5f) / static_cast<float>(COLS);
    float ny = RowToY(row);
    if (nx != hw_cursor_x_) {
        SendParam("WT_CursorX", nx);
        hw_cursor_x_ = nx;
    }
    if (ny != hw_cursor_y_) {
        SendParam("WT_CursorY", ny);
        hw_cursor_y_ = ny;
    }
}

void PDADisplay::SendWrite(int col, float row, int char_idx, bool sleep) {
    MoveCursor(col, row);
    SendParam("WT_CharLo", static_cast<float>(char_idx & 0xFF));
    SendParam("WT_CharHi", static_cast<float>((char_idx >> 8) & 0xFF));
    last_char_idx_ = char_idx;
    total_writes_++;
    last_write_time_ = std::chrono::steady_clock::now();
    if (sleep) {
        SleepMs(write_delay_);
    }
    char ch = (char_idx >= 32 && char_idx <= 126) ? static_cast<char>(char_idx) : ' ';
    screen_.Put(col, static_cast<int>(std::round(row)), ch);
}

void PDADisplay::WriteChar(int col, float row, int char_idx) {
    if (buffered_) {
        write_queue_.emplace_back(col, row, char_idx);
    } else {
        SendWrite(col, row, char_idx, true); // immediate: sleep for write_delay
    }
}

void PDADisplay::WriteText(int col, float row, const std::string& text, bool inverted) {
    int buf_row = static_cast<int>(std::round(row));
    for (int i = 0; i < static_cast<int>(text.size()); i++) {
        char ch = text[i];
        if (ch == ' ' && !inverted) {
            if (col + i >= 0 && col + i < COLS && buf_row >= 0 && buf_row < ROWS) {
                if (screen_.Get(col + i, buf_row) == ' ') {
                    continue;
                }
            }
            WriteChar(col + i, row, 32);
            continue;
        }
        int char_idx = (ch >= 32 && ch <= 126) ? static_cast<int>(ch) : 32;
        if (inverted) char_idx += INVERT_OFFSET;
        WriteChar(col + i, row, char_idx);
    }
}

void PDADisplay::WriteGlyph(int col, float row, int glyph_idx) {
    WriteChar(col, row, glyph_idx);
}

void PDADisplay::WriteBox(int col, int row, int w, int h) {
    WriteGlyph(col, row, G_TL_CORNER);
    WriteGlyph(col + w - 1, row, G_TR_CORNER);
    WriteGlyph(col, row + h - 1, G_BL_CORNER);
    WriteGlyph(col + w - 1, row + h - 1, G_BR_CORNER);
    for (int c = col + 1; c < col + w - 1; c++) {
        WriteGlyph(c, row, G_HLINE);
        WriteGlyph(c, row + h - 1, G_HLINE);
    }
    for (int r = row + 1; r < row + h - 1; r++) {
        WriteGlyph(col, r, G_VLINE);
        WriteGlyph(col + w - 1, r, G_VLINE);
    }
}

void PDADisplay::WriteHLine(int col, float row, int length) {
    for (int c = col; c < col + length; c++) {
        WriteGlyph(c, row, G_HLINE);
    }
}

void PDADisplay::SetMode(Mode mode) {
    Mode prev = current_mode_;
    SendParam("WT_ScaleA", mode == MODE_MACRO);
    SendParam("WT_ScaleB", mode == MODE_CLEAR);
    current_mode_ = mode;
    if (prev == MODE_MACRO && mode == MODE_TEXT) {
        // The Write Head quad renders every frame. After macro→text switch,
        // it shrinks to one cell but cursor is still at center (0.5, 0.5)
        // from the stamp. Send cursor + space char in the same param batch
        // (no sleep) so VRChat processes them in the same frame as the
        // mode switch — the write head lands at the corner with a space.
        SendParam("WT_CursorX", 0.0125f);  // col 0
        SendParam("WT_CursorY", 0.0f);
        SendParam("WT_CharLo", 32.0f);     // space
        hw_cursor_x_ = -1.0f;
        hw_cursor_y_ = -1.0f;
    }
    SleepMs(settle_delay_);
}

void PDADisplay::SetTextMode()  { SetMode(MODE_TEXT); }
void PDADisplay::SetMacroMode() { SetMode(MODE_MACRO); }
void PDADisplay::SetClearMode() { SetMode(MODE_CLEAR); }

void PDADisplay::StampMacro(int macro_index) {
    SendParam("WT_CursorX", 0.5f);
    SendParam("WT_CursorY", 0.5f);
    SendParam("WT_CharLo", static_cast<float>(macro_index));
    SleepMs(write_delay_);
    // Invalidate cached cursor so next text write resends both axes.
    hw_cursor_x_ = -1.0f;
    hw_cursor_y_ = -1.0f;
}

void PDADisplay::ClearScreen() {
    Logger::Debug("Clearing screen");
    SetClearMode();
    SendParam("WT_CursorX", 0.5f);
    SendParam("WT_CursorY", 0.5f);
    hw_cursor_x_ = -1.0f;
    hw_cursor_y_ = -1.0f;
    SendParam("WT_CharLo", 0.0f);
    SleepMs(0.3f);
    screen_.Clear();
}

void PDADisplay::BeginBuffered() {
    if (!buffered_) {
        write_queue_.clear();
    }
    buffered_ = true;
}

bool PDADisplay::FlushOne() {
    if (write_queue_.empty()) {
        buffered_ = false;
        return false;
    }

    // Non-blocking: check if write_delay has elapsed since last write
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - last_write_time_).count();
    if (elapsed < write_delay_) {
        return true; // still waiting — tell caller buffer is active but don't block
    }

    auto [col, row, char_idx] = write_queue_.front();
    write_queue_.erase(write_queue_.begin());
    SendWrite(col, row, char_idx, false); // buffered: no sleep, timing handled by FlushOne
    if (write_queue_.empty()) {
        buffered_ = false;
        return false;
    }
    return true;
}

void PDADisplay::CancelBuffered() {
    buffered_ = false;
    write_queue_.clear();
}

bool PDADisplay::IsBuffered() const {
    return buffered_ && !write_queue_.empty();
}

int PDADisplay::BufferedRemaining() const {
    return static_cast<int>(write_queue_.size());
}

} // namespace YipOS
