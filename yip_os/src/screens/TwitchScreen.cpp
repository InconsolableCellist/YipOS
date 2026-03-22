#include "TwitchScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Logger.hpp"
#include <cstdio>
#include <algorithm>
#include <chrono>

namespace YipOS {

using namespace Glyphs;

static double MonotonicNow() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

TwitchScreen::TwitchScreen(PDAController& pda) : Screen(pda) {
    name = "TWTCH";
    macro_index = FEED_MACRO;
    update_interval = 1.0f;

    SyncMessages();
    last_counter_ = pda_.GetTwitchClient()->GetNewCounter();
    last_redraw_time_ = MonotonicNow();
}

void TwitchScreen::SyncMessages() {
    auto* client = pda_.GetTwitchClient();
    if (client) {
        messages_ = client->GetMessages();
        // Cap at MAX_MESSAGES for pagination
        if (static_cast<int>(messages_.size()) > MAX_MESSAGES)
            messages_.resize(MAX_MESSAGES);
    }
}

int TwitchScreen::PageCount() const {
    int n = static_cast<int>(messages_.size());
    if (n == 0) return 1;
    return (n + MSGS_PER_PAGE - 1) / MSGS_PER_PAGE;
}

int TwitchScreen::VisibleOnPage() const {
    int base = page_ * MSGS_PER_PAGE;
    int remaining = static_cast<int>(messages_.size()) - base;
    return std::clamp(remaining, 0, MSGS_PER_PAGE);
}

void TwitchScreen::Render() {
    RenderFrame("TWITCH");
    RenderAll();
    RenderStatusBar();
}

void TwitchScreen::RenderDynamic() {
    RenderAll();
    RenderClock();
    RenderCursor();
}

void TwitchScreen::RenderAll() {
    auto& d = display_;

    if (messages_.empty()) {
        auto* client = pda_.GetTwitchClient();
        if (client && !client->IsConnected()) {
            std::string ch = client->GetChannel();
            if (ch.empty()) {
                d.WriteText(2, 2, "No channel configured", true);
                d.WriteText(2, 6, "Set twitch.channel in ini");
            } else {
                d.WriteText(2, 2, "Connecting to", true);
                d.WriteText(2, 3, "#" + ch.substr(0, 30), true);
            }
        } else {
            d.WriteText(2, 2, "Waiting for messages...", true);
        }
        RenderConnectionStatus();
        return;
    }

    int base = page_ * MSGS_PER_PAGE;

    // Featured message (rows 1-4, inverted)
    if (base < static_cast<int>(messages_.size())) {
        RenderFeatured();
    }

    // Row 5: 2nd on page
    if (base + 1 < static_cast<int>(messages_.size())) {
        RenderOlderRow(5, base + 1, cursor_ == 1);
    }

    // Row 6: 3rd on page
    if (base + 2 < static_cast<int>(messages_.size())) {
        RenderOlderRow(6, base + 2, cursor_ == 2);
    }

    RenderPageIndicator();
    RenderConnectionStatus();
}

void TwitchScreen::RenderFeatured() {
    auto& d = display_;
    int idx = MsgIndex(0);
    if (idx >= static_cast<int>(messages_.size())) return;
    auto& msg = messages_[idx];

    int max_w = COLS - 2;  // 38
    bool selected = (cursor_ == 0);

    // Row 1: username + timestamp (inverted)
    std::string sender = msg.from;
    std::string time_str = FormatRelativeTime(msg.date);
    int name_max = max_w - static_cast<int>(time_str.size()) - 1;
    if (static_cast<int>(sender.size()) > name_max)
        sender = sender.substr(0, name_max);

    // Selection: first 3 chars normal (un-inverted) to stand out on inverted bg
    if (selected) {
        for (int c = 0; c < SEL_WIDTH && c < static_cast<int>(sender.size()); c++) {
            d.WriteChar(1 + c, 1, static_cast<int>(sender[c]));
        }
        for (int c = SEL_WIDTH; c < static_cast<int>(sender.size()); c++) {
            d.WriteChar(1 + c, 1, static_cast<int>(sender[c]) + INVERT_OFFSET);
        }
    } else {
        d.WriteText(1, 1, sender, true);
    }

    // Timestamp right-aligned (inverted)
    int time_col = COLS - 1 - static_cast<int>(time_str.size());
    d.WriteText(time_col, 1, time_str, true);

    // Rows 2-4: message text word-wrapped (inverted)
    std::string text = msg.text;
    for (auto& ch : text) {
        if (ch == '\n' || ch == '\r') ch = ' ';
    }

    int text_row = 2;
    int chars_on_line = 0;

    for (size_t i = 0; i < text.size() && text_row <= 4; i++) {
        if (chars_on_line >= max_w) {
            text_row++;
            chars_on_line = 0;
            if (text_row > 4) break;
        }
        d.WriteChar(1 + chars_on_line, text_row,
                    static_cast<int>(text[i]) + INVERT_OFFSET);
        chars_on_line++;
    }
}

void TwitchScreen::RenderOlderRow(int row, int msg_index, bool selected) {
    auto& d = display_;
    if (msg_index >= static_cast<int>(messages_.size())) return;

    auto& msg = messages_[msg_index];
    int content_width = COLS - 2;  // 38

    std::string sender = msg.from;
    if (static_cast<int>(sender.size()) > 8)
        sender = sender.substr(0, 8);

    std::string text = msg.text;
    for (auto& ch : text) {
        if (ch == '\n' || ch == '\r') ch = ' ';
    }

    std::string time_str = FormatRelativeTime(msg.date);

    std::string line = sender + ": " + text;

    int time_col = content_width - static_cast<int>(time_str.size());
    if (static_cast<int>(line.size()) > time_col) {
        if (time_col > 3) {
            line = line.substr(0, time_col - 3) + "...";
        } else {
            line = line.substr(0, time_col);
        }
    }
    while (static_cast<int>(line.size()) < time_col)
        line += ' ';

    for (int c = 0; c < static_cast<int>(line.size()); c++) {
        int ch = static_cast<int>(line[c]);
        if (selected && c < SEL_WIDTH) ch += INVERT_OFFSET;
        d.WriteChar(1 + c, row, ch);
    }

    d.WriteText(1 + time_col, row, time_str);
}

void TwitchScreen::WriteSelectionMark(int slot, bool selected) {
    auto& d = display_;
    int idx = MsgIndex(slot);
    if (idx >= static_cast<int>(messages_.size())) return;
    auto& msg = messages_[idx];

    if (slot == 0) {
        // Featured block: first 3 chars of sender on row 1
        std::string sender = msg.from;
        int max_w = COLS - 2;
        std::string time_str = FormatRelativeTime(msg.date);
        int name_max = max_w - static_cast<int>(time_str.size()) - 1;
        if (static_cast<int>(sender.size()) > name_max)
            sender = sender.substr(0, name_max);

        for (int c = 0; c < SEL_WIDTH && c < static_cast<int>(sender.size()); c++) {
            int ch = static_cast<int>(sender[c]);
            // Selected: normal (un-inverted) on inverted bg. Deselected: inverted.
            if (!selected) ch += INVERT_OFFSET;
            d.WriteChar(1 + c, 1, ch);
        }
    } else {
        // Older row: first 3 chars of sender on row 5 or 6
        int row = (slot == 1) ? 5 : 6;
        std::string sender = msg.from;
        if (static_cast<int>(sender.size()) > 8)
            sender = sender.substr(0, 8);

        for (int c = 0; c < SEL_WIDTH && c < static_cast<int>(sender.size()); c++) {
            int ch = static_cast<int>(sender[c]);
            // Selected: inverted on normal bg. Deselected: normal.
            if (selected) ch += INVERT_OFFSET;
            d.WriteChar(1 + c, row, ch);
        }
    }
}

void TwitchScreen::RenderPageIndicator() {
    if (messages_.empty()) return;

    int global_idx = MsgIndex(cursor_) + 1;
    int total = static_cast<int>(messages_.size());
    char pos[12];
    std::snprintf(pos, sizeof(pos), "%d/%d", global_idx, total);
    display_.WriteText(5, 7, pos);

    // Page arrows on left border
    if (page_ > 0) {
        display_.WriteGlyph(0, 3, G_UP);
    }
    if (page_ < PageCount() - 1) {
        display_.WriteGlyph(0, 5, G_DOWN);
    }
}

void TwitchScreen::RenderConnectionStatus() {
    auto* client = pda_.GetTwitchClient();
    if (!client) return;

    if (!client->IsConnected()) {
        display_.WriteText(30, 0, "(DISC)");
    }
}

void TwitchScreen::Update() {
    auto* client = pda_.GetTwitchClient();
    if (!client) return;

    uint64_t counter = client->GetNewCounter();
    if (counter == last_counter_) return;

    last_counter_ = counter;

    // Only auto-redraw on page 0 (newest messages)
    if (page_ != 0) return;

    // Rate-limit redraws
    double now = MonotonicNow();
    if (now - last_redraw_time_ < MIN_REDRAW_INTERVAL) return;

    // Only redraw when buffer is drained
    if (display_.IsBuffered()) return;

    last_redraw_time_ = now;

    // Full redraw: clear screen, stamp macro, render content
    SyncMessages();
    cursor_ = 0;
    pda_.StartRender(this);
}

std::string TwitchScreen::FormatRelativeTime(int64_t date) {
    auto now = std::chrono::system_clock::now();
    int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    int64_t diff = now_ts - date;

    if (diff < 0) diff = 0;

    char buf[8];
    if (diff < 60) {
        return "<1m";
    } else if (diff < 3600) {
        std::snprintf(buf, sizeof(buf), "%ldm", static_cast<long>(diff / 60));
    } else if (diff < 86400) {
        std::snprintf(buf, sizeof(buf), "%ldh", static_cast<long>(diff / 3600));
    } else if (diff < 604800) {
        std::snprintf(buf, sizeof(buf), "%ldd", static_cast<long>(diff / 86400));
    } else {
        std::snprintf(buf, sizeof(buf), "%ldw", static_cast<long>(diff / 604800));
    }
    return buf;
}

bool TwitchScreen::OnInput(const std::string& key) {
    // Joystick: cycle cursor — only redraws the 3-char selection marks
    if (key == "Joystick") {
        int count = VisibleOnPage();
        if (count == 0) return true;
        int old_cursor = cursor_;
        cursor_ = (cursor_ + 1) % count;

        display_.CancelBuffered();
        display_.BeginBuffered();
        WriteSelectionMark(old_cursor, false);
        WriteSelectionMark(cursor_, true);
        RenderPageIndicator();
        return true;
    }

    // TR: select message -> open detail
    if (key == "TR") {
        int idx = MsgIndex(cursor_);
        if (idx < static_cast<int>(messages_.size())) {
            pda_.SetSelectedTwitch(&messages_[idx]);
            pda_.SetPendingNavigate("TWTCH_DTL");
        }
        return true;
    }

    // ML: page up (newer messages)
    if (key == "ML" && page_ > 0) {
        page_--;
        cursor_ = 0;
        SyncMessages();
        pda_.StartRender(this);
        return true;
    }

    // BL: page down (older messages)
    if (key == "BL" && page_ < PageCount() - 1) {
        page_++;
        cursor_ = 0;
        SyncMessages();
        pda_.StartRender(this);
        return true;
    }

    return false;
}

} // namespace YipOS
