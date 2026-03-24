#pragma once

#include "Screen.hpp"
#include <string>
#include <vector>

namespace YipOS {

class INTRPScreen : public Screen {
public:
    INTRPScreen(PDAController& pda);
    ~INTRPScreen() override;

    void Render() override;
    void RenderDynamic() override;
    void Update() override;
    bool OnInput(const std::string& key) override;

private:
    void StartINTRP();
    void StopINTRP();
    bool FilterText(const std::string& text) const;
    void WordWrap(const std::string& text, std::vector<std::string>& output);
    void WriteInverted(int col, int row, const std::string& text);

    // Top half: their speech (rows 1-3)
    static constexpr int TOP_FIRST_ROW = 1;
    static constexpr int TOP_LAST_ROW = 3;
    int top_cursor_ = TOP_FIRST_ROW;
    std::vector<std::string> pending_top_;

    // Bottom half: your speech (rows 5-6)
    static constexpr int BOT_FIRST_ROW = 5;
    static constexpr int BOT_LAST_ROW = 6;
    int bot_cursor_ = BOT_FIRST_ROW;
    std::vector<std::string> pending_bot_;

    static constexpr int LINE_WIDTH = 38;
    static constexpr int LEFT_COL = 1;
    static constexpr int RIGHT_COL = 38;
    static constexpr size_t MAX_PENDING = 20;
    static constexpr int LINES_PER_TICK = 2;

    bool started_by_screen_ = false;
};

} // namespace YipOS
