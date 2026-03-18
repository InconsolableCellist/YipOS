#include "VRCXNotifScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace YipOS {

using namespace Glyphs;

VRCXNotifScreen::VRCXNotifScreen(PDAController& pda) : Screen(pda) {
    name = "NOTIF";
    macro_index = 20;
    last_seen_at_ = pda_.GetConfig().GetState("notif.last_seen");
    LoadData();
    // Mark notifications as seen now that we're viewing them
    pda_.MarkNotifsSeen();
}

void VRCXNotifScreen::LoadData() {
    auto* vrcx = pda_.GetVRCXData();
    if (vrcx && vrcx->IsOpen()) {
        notifs_ = vrcx->GetNotifications(300);
    }
}

int VRCXNotifScreen::PageCount() const {
    int n = static_cast<int>(notifs_.size());
    if (n == 0) return 1;
    return (n + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
}

int VRCXNotifScreen::ItemCountOnPage() const {
    if (notifs_.empty()) return 0;
    int base = page_ * ROWS_PER_PAGE;
    int remaining = static_cast<int>(notifs_.size()) - base;
    return std::min(remaining, ROWS_PER_PAGE);
}

void VRCXNotifScreen::Render() {
    RenderFrame("NOTIF");
    RenderRows();
    RenderPageIndicators();
    RenderStatusBar();
}

void VRCXNotifScreen::RenderDynamic() {
    // Check confirmation / done timeout
    if (clear_confirming_ || clear_done_) {
        double now = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        double timeout = clear_done_ ? DONE_TIMEOUT : CONFIRM_TIMEOUT;
        if ((now - clear_confirm_time_) >= timeout) {
            clear_confirming_ = false;
            clear_done_ = false;
            refresh_interval = 0;
        }
    }

    RenderRows();
    RenderClearButton();
    RenderPageIndicators();
    RenderClock();
    RenderCursor();
}

void VRCXNotifScreen::RenderRow(int i, bool selected) {
    auto& d = display_;
    int idx = page_ * ROWS_PER_PAGE + i;
    int row = 1 + i;
    if (idx >= static_cast<int>(notifs_.size())) return;

    auto& n = notifs_[idx];

    // Unseen indicator: "*" if this notification is newer than last_seen_at_
    bool unseen = !last_seen_at_.empty() ? (n.created_at > last_seen_at_) : true;
    char indicator = unseen ? '*' : ' ';

    // Format: "*TYPE sender      HH:MM"
    std::string type_str = FormatType(n.type);
    std::string time_str = FormatTime(n.created_at);
    int time_len = static_cast<int>(time_str.size());
    int content_width = COLS - 2;

    // indicator(1) + type(4) + space(1) + sender + time
    int sender_max = content_width - time_len - 6;
    std::string sender = n.sender;
    if (static_cast<int>(sender.size()) > sender_max) {
        sender = sender.substr(0, sender_max);
    }

    std::string line;
    line += indicator;
    line += type_str;
    line += ' ';
    line += sender;

    // First 3 chars inverted = selection indicator
    static constexpr int SEL_WIDTH = 3;

    for (int c = 0; c < static_cast<int>(line.size()); c++) {
        int ch = static_cast<int>(line[c]);
        if (selected && c < SEL_WIDTH) ch += INVERT_OFFSET;
        d.WriteChar(1 + c, row, ch);
    }

    // Time right-justified
    int time_col = COLS - 1 - time_len;
    d.WriteText(time_col, row, time_str);
}

void VRCXNotifScreen::RenderRows() {
    auto& d = display_;

    if (notifs_.empty()) {
        d.WriteText(2, 3, "No notifications");
        return;
    }

    int items = ItemCountOnPage();
    for (int i = 0; i < items; i++) {
        RenderRow(i, i == cursor_);
    }
}

void VRCXNotifScreen::RefreshCursorRows(int old_cursor, int new_cursor) {
    display_.CancelBuffered();
    display_.BeginBuffered();
    if (old_cursor != new_cursor && old_cursor >= 0 && old_cursor < ItemCountOnPage()) {
        RenderRow(old_cursor, false);
    }
    if (new_cursor >= 0 && new_cursor < ItemCountOnPage()) {
        RenderRow(new_cursor, true);
    }
    RenderPageIndicators();
}

void VRCXNotifScreen::RenderPageIndicators() {
    auto& d = display_;

    if (!notifs_.empty()) {
        int global_idx = page_ * ROWS_PER_PAGE + cursor_ + 1;
        int total = static_cast<int>(notifs_.size());
        char pos[12];
        std::snprintf(pos, sizeof(pos), "%d/%d", global_idx, total);
        d.WriteText(5, 7, pos);
    }

    if (PageCount() <= 1) return;

    if (page_ > 0) {
        d.WriteGlyph(0, 3, G_UP);
    }
    if (page_ < PageCount() - 1) {
        d.WriteGlyph(0, 5, G_DOWN);
    }
}

std::string VRCXNotifScreen::FormatTime(const std::string& created_at) {
    // "2026-03-15 12:34:56" → "HH:MM"
    if (created_at.size() >= 16) {
        return created_at.substr(11, 5);
    }
    return "     ";
}

std::string VRCXNotifScreen::FormatType(const std::string& type) {
    // Compact 4-char type labels
    if (type == "invite") return "INVT";
    if (type == "friendRequest") return "FREQ";
    if (type == "requestInvite") return "RINV";
    if (type == "requestInviteResponse") return "RRES";
    if (type == "votetokick") return "KICK";
    if (type == "informational") return "INFO";
    // Fallback: first 4 chars uppercased
    std::string out = type.substr(0, 4);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

void VRCXNotifScreen::RenderClearButton() {
    // Button text centered on BTN_CENTER (col 20), rows 5-6
    const char* line1 = clear_done_ ? " DONE  " : (clear_confirming_ ? " SUR?  " : "CLR ALL");
    const char* line2 = clear_done_ ? "       " : (clear_confirming_ ? "       " : "NOTIFS ");
    int l1 = static_cast<int>(std::strlen(line1));
    int l2 = static_cast<int>(std::strlen(line2));
    display_.WriteText(BTN_CENTER - l1 / 2, 5, line1, true);
    display_.WriteText(BTN_CENTER - l2 / 2, 6, line2, true);
}

bool VRCXNotifScreen::OnInput(const std::string& key) {
    // Joystick: cycle cursor
    if (key == "Joystick") {
        int items = ItemCountOnPage();
        if (items == 0) return true;
        int old_cursor = cursor_;
        cursor_ = (cursor_ + 1) % items;
        RefreshCursorRows(old_cursor, cursor_);
        return true;
    }

    // 33 (touch center-bottom): CLR ALL NOTIFS with double-tap confirmation
    if (key == "33") {
        double now = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        display_.CancelBuffered();

        if (clear_confirming_ && (now - clear_confirm_time_) < CONFIRM_TIMEOUT) {
            // Second tap — actually clear
            pda_.ClearAllNotifs();
            if (!notifs_.empty()) {
                last_seen_at_ = notifs_[0].created_at;
            }
            clear_confirming_ = false;
            clear_done_ = true;
            clear_confirm_time_ = now;
            refresh_interval = 1.0f;
            Logger::Info("Notifications cleared");
        } else {
            // First tap — show confirmation
            clear_confirming_ = true;
            clear_done_ = false;
            clear_confirm_time_ = now;
            refresh_interval = 1.0f;
        }

        display_.BeginBuffered();
        RenderClearButton();
        RenderRows();
        return true;
    }

    // ML/BL: page up/down
    if (key == "ML" && PageCount() > 1 && page_ > 0) {
        page_--;
        cursor_ = 0;
        pda_.StartRender(this);
        return true;
    }
    if (key == "BL" && PageCount() > 1 && page_ < PageCount() - 1) {
        page_++;
        cursor_ = 0;
        pda_.StartRender(this);
        return true;
    }

    return false;
}

} // namespace YipOS
