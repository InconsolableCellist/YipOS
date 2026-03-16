#pragma once

#include "Screen.hpp"
#include <vector>
#include <string>

namespace YipOS {

class CCScreen : public Screen {
public:
    CCScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    void Update() override;
    bool OnInput(const std::string& key) override;

private:
    void RenderLines();
    void WriteInverted(int col, int row, const std::string& text);
    void WrapAndAppend(const std::string& text);

    // Scrolling text buffer — 6 visible rows, each up to 38 chars
    std::vector<std::string> lines_;
    static constexpr int VISIBLE_ROWS = 6;
    static constexpr int LINE_WIDTH = 38; // COLS - 2 (borders)
};

} // namespace YipOS
