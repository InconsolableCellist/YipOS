#include "Screen.hpp"
#include "HomeScreen.hpp"
#include "StatsScreen.hpp"
#include "NetScreen.hpp"
#include "HeartScreen.hpp"
#include "StayScreen.hpp"
#include "CalibrateScreen.hpp"
#include "ConfScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Glyphs.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace YipOS {

using namespace Glyphs;

Screen::Screen(PDAController& pda)
    : pda_(pda), display_(pda.GetDisplay()) {}

void Screen::Render() {
    RenderFrame(name);
    RenderContent();
    RenderStatusBar();
}

void Screen::RenderContent() {}

void Screen::RenderDynamic() {
    RenderClock();
    RenderCursor();
}

bool Screen::OnInput(const std::string& key) {
    return false;
}

void Screen::Update() {}

void Screen::RenderFrame(const std::string& title) {
    display_.WriteGlyph(0, 0, G_TL_CORNER);
    std::string title_str = " " + title + " ";
    int pad_left = (COLS - 2 - static_cast<int>(title_str.size())) / 2;
    for (int c = 1; c < 1 + pad_left; c++) {
        display_.WriteGlyph(c, 0, G_HLINE);
    }
    display_.WriteText(1 + pad_left, 0, title_str);
    for (int c = 1 + pad_left + static_cast<int>(title_str.size()); c < COLS - 1; c++) {
        display_.WriteGlyph(c, 0, G_HLINE);
    }
    display_.WriteGlyph(COLS - 1, 0, G_TR_CORNER);

    for (int r = 1; r < 7; r++) {
        display_.WriteGlyph(0, r, G_VLINE);
        display_.WriteGlyph(COLS - 1, r, G_VLINE);
    }
}

void Screen::RenderStatusBar() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%H:%M:%S");
    std::string clock_str = ss.str();

    display_.WriteGlyph(0, 7, G_BL_CORNER);
    RenderCursor();
    int time_start = COLS - 1 - static_cast<int>(clock_str.size());
    for (int c = 2; c < time_start; c++) {
        display_.WriteGlyph(c, 7, G_HLINE);
    }
    display_.WriteText(time_start, 7, clock_str);
    display_.WriteGlyph(COLS - 1, 7, G_BR_CORNER);
}

void Screen::RenderClock() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%H:%M:%S");
    std::string clock_str = ss.str();
    int col = COLS - 1 - static_cast<int>(clock_str.size());
    display_.WriteText(col, 7, clock_str);
}

void Screen::RenderCursor() {
    char ch = pda_.GetSpinnerChar();
    display_.WriteChar(1, 7, static_cast<int>(ch));
}

std::unique_ptr<Screen> CreateScreen(const std::string& name, PDAController& pda) {
    if (name == "STATS") return std::make_unique<StatsScreen>(pda);
    if (name == "NET")   return std::make_unique<NetScreen>(pda);
    if (name == "HEART") return std::make_unique<HeartScreen>(pda);
    if (name == "SPVR")  return std::make_unique<StayScreen>(pda);
    if (name == "CONF") return std::make_unique<ConfScreen>(pda);
    if (name == "DBG")  return std::make_unique<CalibrateScreen>(pda);
    return nullptr;
}

} // namespace YipOS
