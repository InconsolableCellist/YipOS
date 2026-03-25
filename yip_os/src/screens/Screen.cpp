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
#include "ChatScreen.hpp"
#include "ChatDetailScreen.hpp"
#include "IMGScreen.hpp"
#include "TEXTScreen.hpp"
#include "MediaScreen.hpp"
#include "StonkScreen.hpp"
#include "StonkListScreen.hpp"
#include "TwitchScreen.hpp"
#include "TwitchDetailScreen.hpp"
#include "INTRPScreen.hpp"
#include "INTRPConfScreen.hpp"
#include "INTRPLangScreen.hpp"
#include "QRTestScreen.hpp"
#include "DMScreen.hpp"
#include "DMDetailScreen.hpp"
#include "DMPairScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Glyphs.hpp"
#include "core/TimeUtil.hpp"
#include <unordered_map>

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
    std::string clock_str = FormatClockString();

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
    std::string clock_str = FormatClockString();
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
    // Col 3: notification indicator (VRCX notifs OR chat/DM unseen)
    if (pda_.HasUnseenNotifsCached() || pda_.HasUnseenChatCached() || pda_.HasUnseenDMCached()) {
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
    using Factory = std::unique_ptr<Screen>(*)(PDAController&);
    static const std::unordered_map<std::string, Factory> registry = {
        {"STATS",              [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<StatsScreen>(p); }},
        {"NET",                [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<NetScreen>(p); }},
        {"HEART",              [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<HeartScreen>(p); }},
        {"SPVR",               [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<StayScreen>(p); }},
        {"CONF",               [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<ConfScreen>(p); }},
        {"DBG",                [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<CalibrateScreen>(p); }},
        {"VRCX",               [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<VRCXScreen>(p); }},
        {"VRCX_WORLDS",        [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<VRCXWorldsScreen>(p); }},
        {"VRCX_WORLD_DETAIL",  [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<VRCXWorldDetailScreen>(p); }},
        {"VRCX_FEED",          [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<VRCXFeedScreen>(p); }},
        {"VRCX_FEED_DETAIL",   [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<VRCXFeedDetailScreen>(p); }},
        {"VRCX_FRIEND_DETAIL", [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<VRCXFriendDetailScreen>(p); }},
        {"CC",                 [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<CCScreen>(p); }},
        {"CC_CONF",            [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<CCConfScreen>(p); }},
        {"AVTR",               [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<AVTRScreen>(p); }},
        {"AVTR_CHANGE",        [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<AVTRChangeScreen>(p); }},
        {"AVTR_DETAIL",        [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<AVTRDetailScreen>(p); }},
        {"AVTR_CTRL",          [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<AVTRCtrlScreen>(p); }},
        {"VRCX_NOTIF",         [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<VRCXNotifScreen>(p); }},
        {"LOCK",               [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<LockScreen>(p); }},
        {"BFI",                [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<BFIScreen>(p); }},
        {"BFI_PARAM",          [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<BFIParamScreen>(p); }},
        {"CHAT",               [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<ChatScreen>(p); }},
        {"CHAT_DTL",           [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<ChatDetailScreen>(p); }},
        {"IMG",                [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<IMGScreen>(p); }},
        {"TEXT",               [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<TEXTScreen>(p); }},
        {"MEDIA",              [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<MediaScreen>(p); }},
        {"STONK",              [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<StonkScreen>(p); }},
        {"STONK_LIST",         [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<StonkListScreen>(p); }},
        {"TWTCH",              [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<TwitchScreen>(p); }},
        {"TWTCH_DTL",          [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<TwitchDetailScreen>(p); }},
        {"INTRP",              [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<INTRPScreen>(p); }},
        {"INTRP_CONF",         [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<INTRPConfScreen>(p); }},
        {"INTRP_LANG",         [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<INTRPLangScreen>(p); }},
        {"QRTEST",             [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<QRTestScreen>(p); }},
        {"DM",                 [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<DMScreen>(p); }},
        {"DM_DTL",             [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<DMDetailScreen>(p); }},
        {"DM_PAIR",            [](PDAController& p) -> std::unique_ptr<Screen> { return std::make_unique<DMPairScreen>(p); }},
    };

    auto it = registry.find(name);
    if (it != registry.end()) return it->second(pda);
    return nullptr;
}

} // namespace YipOS
