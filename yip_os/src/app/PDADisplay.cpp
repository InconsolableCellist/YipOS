#include "PDADisplay.hpp"
#include "net/OSCManager.hpp"
#include "core/Logger.hpp"
#include <algorithm>
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

void PDADisplay::SendParam(const std::string& name, int value) {
    osc_.SendInt(std::string(PARAM_PREFIX) + name, value);
}

void PDADisplay::SendParam(const std::string& name, bool value) {
    osc_.SendBool(std::string(PARAM_PREFIX) + name, value);
}

void PDADisplay::MoveCursor(int col, float row) {
    float nx, ny;
    if (current_mode_ == MODE_BITMAP) {
        // Bitmap mode: 32x32 grid, linear normalization (no Y calibration)
        nx = (col + 0.5f) / 32.0f;
        ny = (row + 0.5f) / 32.0f;
    } else {
        nx = (col + 0.5f) / static_cast<float>(COLS);
        ny = RowToY(row);
    }
    if (nx != hw_cursor_x_) {
        SendParam("WT_CursorX", nx);
        hw_cursor_x_ = nx;
    }
    if (ny != hw_cursor_y_) {
        SendParam("WT_CursorY", ny);
        hw_cursor_y_ = ny;
    }
}

void PDADisplay::SendWrite(int col, float row, int char_idx, int bank, bool sleep) {
    int buf_row = static_cast<int>(std::round(row));
    // Skip the OSC round-trip if this cell already holds the requested glyph
    // (and we wouldn't be flipping the bank). This is what makes
    // re-renders cheap — RenderRow on cursor move ends up paying only for
    // the cells that actually flip inverted, not the whole row.
    if (bank == current_bank_ &&
        screen_.Get(col, buf_row) == char_idx) {
        return;
    }
    // Switch ROM bank if needed
    if (bank != current_bank_) {
        SendParam("WT_Bank", bank > 0);
        current_bank_ = bank;
    }
    MoveCursor(col, row);
    SendParam("WT_CharLo", char_idx & 0xFF);
    SendParam("WT_CharHi", (char_idx >> 8) & 0xFF);
    last_char_idx_ = char_idx;
    total_writes_++;
    last_write_time_ = std::chrono::steady_clock::now();
    if (sleep) {
        SleepMs(write_delay_);
    }
    screen_.Put(col, buf_row, char_idx);
}

void PDADisplay::WriteChar(int col, float row, int char_idx, int bank) {
    if (buffered_) {
        if (priority_insert_pos_ >= 0) {
            write_queue_.insert(write_queue_.begin() + priority_insert_pos_,
                                std::make_tuple(col, row, char_idx, bank));
            priority_insert_pos_++;
        } else {
            write_queue_.emplace_back(col, row, char_idx, bank);
        }
    } else {
        SendWrite(col, row, char_idx, bank, true); // immediate: sleep for write_delay
    }
}

void PDADisplay::WriteText(int col, float row, const std::string& text, bool inverted) {
    // No need to short-circuit per-char here — SendWrite already skips writes
    // whose target cell already holds the requested glyph. Just translate
    // each char to its glyph index.
    for (int i = 0; i < static_cast<int>(text.size()); i++) {
        char ch = text[i];
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
    SendParam("WT_ScaleA", mode == MODE_MACRO || mode == MODE_BITMAP);
    SendParam("WT_ScaleB", mode == MODE_CLEAR || mode == MODE_BITMAP);
    current_mode_ = mode;
    if (prev == MODE_MACRO && mode == MODE_TEXT) {
        // The Write Head quad renders every frame. After macro→text switch,
        // it shrinks to one cell but cursor is still at center (0.5, 0.5)
        // from the stamp. Send cursor + space char in the same param batch
        // (no sleep) so VRChat processes them in the same frame as the
        // mode switch — the write head lands at the corner with a space.
        SendParam("WT_CursorX", 0.0125f);  // col 0
        SendParam("WT_CursorY", 0.0f);
        SendParam("WT_CharLo", 32);          // space
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
    SendParam("WT_CharLo", macro_index);
    SleepMs(write_delay_);
    // Invalidate cached cursor so next text write resends both axes.
    hw_cursor_x_ = -1.0f;
    hw_cursor_y_ = -1.0f;
    // The RT now holds whatever glyphs the macro contains, but we don't track
    // those per-cell. Clearing the buffer to UNKNOWN forces the next write
    // to each cell to actually send (otherwise, writes that happen to
    // match the buffer's stale state — e.g. ' ' over a macro border — would
    // be incorrectly skipped).
    screen_.Invalidate();
}

void PDADisplay::ClearScreen() {
    Logger::Debug("Clearing screen");
    // Reset to bank 0 before clearing
    if (current_bank_ != 0) {
        SendParam("WT_Bank", false);
        current_bank_ = 0;
    }
    SetClearMode();
    SendParam("WT_CursorX", 0.5f);
    SendParam("WT_CursorY", 0.5f);
    hw_cursor_x_ = -1.0f;
    hw_cursor_y_ = -1.0f;
    SendParam("WT_CharLo", 0);
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

    auto [col, row, char_idx, bank] = write_queue_.front();
    write_queue_.erase(write_queue_.begin());
    SendWrite(col, row, char_idx, bank, false); // buffered: no sleep, timing handled by FlushOne
    if (write_queue_.empty()) {
        buffered_ = false;
        return false;
    }
    return true;
}

void PDADisplay::CancelBuffered() {
    buffered_ = false;
    priority_insert_pos_ = -1;
    write_queue_.clear();
}

void PDADisplay::BeginPriority() {
    // Priority writes insert at the front of the queue.
    // If not already buffered, start buffering.
    buffered_ = true;
    priority_insert_pos_ = 0;
}

void PDADisplay::EndPriority() {
    if (priority_insert_pos_ <= 0) {
        priority_insert_pos_ = -1;
        return;
    }

    // Remove later queue entries that write to the same (col, row) as any
    // priority write — they carry stale content that would overwrite the
    // priority values (e.g. selection marks).
    int pcount = priority_insert_pos_;
    priority_insert_pos_ = -1;

    auto tail_begin = write_queue_.begin() + pcount;
    auto tail_end = write_queue_.end();
    write_queue_.erase(
        std::remove_if(tail_begin, tail_end,
            [&](const std::tuple<int, float, int, int>& entry) {
                int col = std::get<0>(entry);
                float row = std::get<1>(entry);
                for (int i = 0; i < pcount; i++) {
                    if (std::get<0>(write_queue_[i]) == col &&
                        std::get<1>(write_queue_[i]) == row) {
                        return true;
                    }
                }
                return false;
            }),
        tail_end);
}

bool PDADisplay::IsBuffered() const {
    return buffered_ && !write_queue_.empty();
}

int PDADisplay::BufferedRemaining() const {
    return static_cast<int>(write_queue_.size());
}

} // namespace YipOS
