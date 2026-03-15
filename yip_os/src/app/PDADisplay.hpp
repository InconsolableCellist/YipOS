#pragma once

#include <string>
#include <vector>
#include <tuple>
#include <chrono>
#include "ScreenBuffer.hpp"

namespace YipOS {

class OSCManager;

class PDADisplay {
public:
    enum Mode { MODE_TEXT = 0, MODE_MACRO = 1, MODE_CLEAR = 2 };

    PDADisplay(OSCManager& osc, ScreenBuffer& screen,
               float y_offset = 0.0f, float y_scale = 1.0f, float y_curve = 1.0f,
               float write_delay = 0.07f, float settle_delay = 0.04f);

    // Y calibration
    float RowToY(float row) const;

    // Cursor
    void MoveCursor(int col, float row);

    // Character writes
    void WriteChar(int col, float row, int char_idx);
    void WriteText(int col, float row, const std::string& text, bool inverted = false);
    void WriteGlyph(int col, float row, int glyph_idx);
    void WriteBox(int col, int row, int w, int h);
    void WriteHLine(int col, float row, int length);

    // Mode switching
    void SetMode(Mode mode);
    void SetTextMode();
    void SetMacroMode();
    void SetClearMode();
    void StampMacro(int macro_index);
    void ClearScreen();

    // Buffered writes
    void BeginBuffered();
    bool FlushOne();
    void CancelBuffered();
    bool IsBuffered() const;
    int BufferedRemaining() const;

    // Access
    ScreenBuffer& GetScreen() { return screen_; }
    float GetYOffset() const { return y_offset_; }
    float GetYScale() const { return y_scale_; }
    float GetYCurve() const { return y_curve_; }
    void SetYOffset(float v) { y_offset_ = v; }
    void SetYScale(float v) { y_scale_ = v; }
    void SetYCurve(float v) { y_curve_ = v; }
    float GetWriteDelay() const { return write_delay_; }
    void SetWriteDelay(float v) { write_delay_ = v; }
    float GetSettleDelay() const { return settle_delay_; }
    void SetSettleDelay(float v) { settle_delay_ = v; }

private:
    void SleepMs(float seconds);
    void SendParam(const std::string& name, float value);
    void SendParam(const std::string& name, bool value);
    void SendWrite(int col, float row, int char_idx, bool sleep = true);

    OSCManager& osc_;
    ScreenBuffer& screen_;
    float y_offset_;
    float y_scale_;
    float y_curve_;
    float hw_cursor_x_ = -1.0f;
    float hw_cursor_y_ = -1.0f;
    float write_delay_;
    float settle_delay_;
    Mode current_mode_ = MODE_TEXT;

    // Buffered write queue: (col, row_float, char_idx)
    std::vector<std::tuple<int, float, int>> write_queue_;
    bool buffered_ = false;
    std::chrono::steady_clock::time_point last_write_time_{};
};

} // namespace YipOS
