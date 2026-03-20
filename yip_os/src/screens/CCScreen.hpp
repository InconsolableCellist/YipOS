#pragma once

#include "Screen.hpp"
#include <string>
#include <vector>

namespace YipOS {

class CCScreen : public Screen {
public:
    CCScreen(PDAController& pda);
    ~CCScreen() override;

    void Render() override;
    void RenderDynamic() override;
    void Update() override;
    bool OnInput(const std::string& key) override;

private:
    void WriteInverted(int col, int row, const std::string& text);
    void StartCC();
    void StopCC();
    bool FilterText(const std::string& text) const;

    // Rolling text state
    int cursor_col_ = 1;
    int cursor_row_ = 1;
    static constexpr int FIRST_ROW = 1;
    static constexpr int LAST_ROW = 6;
    static constexpr int LEFT_COL = 1;
    static constexpr int RIGHT_COL = 38; // COLS - 2

    // Pending lines to write (word-wrapped, FIFO)
    std::vector<std::string> pending_lines_;
    int line_char_pos_ = 0;
    static constexpr int LINE_WIDTH = 38;
    static constexpr size_t MAX_PENDING_LINES = 20;
    static constexpr int LINES_PER_TICK = 3;

    bool started_by_screen_ = false; // we auto-started CC
};

} // namespace YipOS
