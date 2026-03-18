#include "Screen.hpp"
#include "HomeScreen.hpp"
#include "StatsScreen.hpp"
#include "NetScreen.hpp"
#include "HeartScreen.hpp"
#include "StayScreen.hpp"
#include "CalibrateScreen.hpp"
#include "ConfScreen.hpp"
#include "VRCXScreen.hpp"
#include "VRCXWorldsScreen.hpp"
#include "VRCXWorldDetailScreen.hpp"
#include "VRCXFeedScreen.hpp"
#include "VRCXFeedDetailScreen.hpp"
#include "VRCXFriendDetailScreen.hpp"
#include "CCScreen.hpp"
#include "CCConfScreen.hpp"
#include "AVTRScreen.hpp"
#include "AVTRChangeScreen.hpp"
#include "AVTRDetailScreen.hpp"
#include "AVTRCtrlScreen.hpp"
#include "VRCXNotifScreen.hpp"
#include "LockScreen.hpp"
#include "BFIScreen.hpp"
#include "BFIParamScreen.hpp"
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
    RenderStatusIcons();
    int time_start = COLS - 1 - static_cast<int>(clock_str.size());
    for (int c = 4; c < time_start; c++) {
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
    RenderStatusIcons();
}

void Screen::RenderStatusIcons() {
    // Col 2: lock indicator when soft-locked
    if (pda_.IsSoftLocked()) {
        bool flashing = pda_.IsLockFlashing();
        int glyph = flashing ? (G_LOCK_INV) : G_LOCK;
        display_.WriteChar(2, 7, glyph);
    } else {
        display_.WriteGlyph(2, 7, G_HLINE);
    }
    // Col 3: notification indicator
    if (pda_.HasUnseenNotifsCached()) {
        display_.WriteGlyph(3, 7, G_BULLET);
    } else {
        display_.WriteGlyph(3, 7, G_HLINE);
    }
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
    if (name == "VRCX") return std::make_unique<VRCXScreen>(pda);
    if (name == "VRCX_WORLDS") return std::make_unique<VRCXWorldsScreen>(pda);
    if (name == "VRCX_WORLD_DETAIL") return std::make_unique<VRCXWorldDetailScreen>(pda);
    if (name == "VRCX_FEED") return std::make_unique<VRCXFeedScreen>(pda);
    if (name == "VRCX_FEED_DETAIL") return std::make_unique<VRCXFeedDetailScreen>(pda);
    if (name == "VRCX_FRIEND_DETAIL") return std::make_unique<VRCXFriendDetailScreen>(pda);
    if (name == "CC") return std::make_unique<CCScreen>(pda);
    if (name == "CC_CONF") return std::make_unique<CCConfScreen>(pda);
    if (name == "AVTR") return std::make_unique<AVTRScreen>(pda);
    if (name == "AVTR_CHANGE") return std::make_unique<AVTRChangeScreen>(pda);
    if (name == "AVTR_DETAIL") return std::make_unique<AVTRDetailScreen>(pda);
    if (name == "AVTR_CTRL") return std::make_unique<AVTRCtrlScreen>(pda);
    if (name == "VRCX_NOTIF") return std::make_unique<VRCXNotifScreen>(pda);
    if (name == "LOCK") return std::make_unique<LockScreen>(pda);
    if (name == "BFI") return std::make_unique<BFIScreen>(pda);
    if (name == "BFI_PARAM") return std::make_unique<BFIParamScreen>(pda);
    return nullptr;
}

} // namespace YipOS
