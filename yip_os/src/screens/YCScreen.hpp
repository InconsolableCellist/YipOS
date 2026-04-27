#pragma once

#include "Screen.hpp"
#include <string>
#include <array>

namespace YipOS {

class YCScreen : public Screen {
public:
    YCScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    void Update() override;
    bool OnInput(const std::string& key) override;

private:
    void ComputeCells();
    void WordWrap(const std::string& text, int start_row, int max_rows, bool invert);
    void FlushDiff();
    void FlashAndDraw();
    void FlushRefresh();

    static constexpr int TEXT_COLS = 38; // COLS - 2 (frame borders)
    static constexpr int TEXT_ROWS = 6;  // rows 1-6
    static constexpr int TOTAL_CELLS = TEXT_COLS * TEXT_ROWS;

    std::array<int, TOTAL_CELLS> desired_{};
    std::array<int, TOTAL_CELLS> displayed_{};

    std::string last_display_text_;
    std::string last_expression_;
    std::string last_thought_;
    bool last_thinking_ = false;
    bool last_show_thoughts_ = false;
    bool last_connected_ = false;

    bool show_thoughts_ = false;

    int refresh_pos_ = 0;
    static constexpr int REFRESH_CHUNK = 12;

    int thinking_frame_ = 0;
};

} // namespace YipOS
