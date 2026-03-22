#include "ChatScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace YipOS {

using namespace Glyphs;

ChatScreen::ChatScreen(PDAController& pda) : Screen(pda) {
    name = "CHAT";

    // Check consent
    std::string consent = pda_.GetConfig().GetState("chat.consent");
    if (!consent.empty()) {
        mode_ = FEED;
        macro_index = FEED_MACRO;

        // Load messages from ChatClient
        messages_ = pda_.GetChatClient().GetMessages();

        // Mark seen on entry
        if (!messages_.empty()) {
            pda_.MarkChatSeen();
            // Refresh seen state from client
            messages_ = pda_.GetChatClient().GetMessages();
        }
    } else {
        mode_ = CONSENT;
        macro_index = CONSENT_MACRO;
        consent_shown_at_ = std::chrono::steady_clock::now();
    }
}

int ChatScreen::PageCount() const {
    int n = static_cast<int>(messages_.size());
    if (n == 0) return 1;
    return (n + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
}

int ChatScreen::ItemCountOnPage() const {
    if (messages_.empty()) return 0;
    int base = page_ * ROWS_PER_PAGE;
    int remaining = static_cast<int>(messages_.size()) - base;
    return std::min(remaining, ROWS_PER_PAGE);
}

void ChatScreen::Render() {
    if (mode_ == CONSENT) {
        RenderFrame("CHAT");
        RenderConsent();
        RenderStatusBar();
    } else {
        RenderFrame("CHAT");
        RenderRows();
        RenderPageIndicators();
        RenderStatusBar();
    }
}

void ChatScreen::RenderDynamic() {
    if (mode_ == CONSENT) {
        RenderConsent();
    } else {
        RenderRows();
        RenderPageIndicators();
    }
    RenderClock();
    RenderCursor();
}

void ChatScreen::RenderConsent() {
    // Static text is in the macro; nothing extra to render dynamically
}

void ChatScreen::RenderRow(int i, bool selected) {
    auto& d = display_;
    int idx = page_ * ROWS_PER_PAGE + i;
    int row = 1 + i;
    if (idx >= static_cast<int>(messages_.size())) return;

    auto& msg = messages_[idx];

    // Format: *Alice: nice avatar lol...       <1m
    // Col 1: unseen indicator (1 char)
    // Col 2-7: sender name truncated to 6 (6 chars)
    // Col 8-9: ": " (2 chars)
    // Col 10-34: message text truncated (25 chars)
    // Col 36-39: relative time right-aligned (4 chars)

    static constexpr int SEL_WIDTH = 3;
    int content_width = COLS - 2; // 38

    // Build the line
    char indicator = msg.seen ? ' ' : '*';

    // Sender name (max 6 chars)
    std::string sender = msg.from;
    if (static_cast<int>(sender.size()) > 6)
        sender = sender.substr(0, 6);

    // Message text (max 25 chars)
    std::string text = msg.text;
    // Replace newlines with spaces
    for (auto& ch : text) {
        if (ch == '\n' || ch == '\r') ch = ' ';
    }
    if (static_cast<int>(text.size()) > 25) {
        text = text.substr(0, 22) + "...";
    }

    // Relative timestamp
    std::string time_str = FormatRelativeTime(msg.date);

    // Build full line
    std::string line;
    line += indicator;
    line += sender;
    line += ": ";
    line += text;

    // Pad to leave room for time
    int time_col = content_width - static_cast<int>(time_str.size());
    while (static_cast<int>(line.size()) < time_col)
        line += ' ';
    // Truncate if overflows into time area
    if (static_cast<int>(line.size()) > time_col)
        line = line.substr(0, time_col);

    // Write line with selection highlighting
    for (int c = 0; c < static_cast<int>(line.size()); c++) {
        int ch = static_cast<int>(line[c]);
        if (selected && c < SEL_WIDTH) ch += INVERT_OFFSET;
        d.WriteChar(1 + c, row, ch);
    }

    // Write time right-aligned
    d.WriteText(1 + time_col, row, time_str);
}

void ChatScreen::RenderRows() {
    auto& d = display_;

    if (messages_.empty()) {
        d.WriteText(2, 3, "No messages yet");
        return;
    }

    int items = ItemCountOnPage();
    for (int i = 0; i < items; i++) {
        RenderRow(i, i == cursor_);
    }
}

void ChatScreen::WriteSelectionMark(int i, bool selected) {
    int idx = page_ * ROWS_PER_PAGE + i;
    int row = 1 + i;
    if (idx >= static_cast<int>(messages_.size())) return;

    auto& msg = messages_[idx];
    char chars[3];
    chars[0] = msg.seen ? ' ' : '*';
    chars[1] = msg.from.size() > 0 ? msg.from[0] : ' ';
    chars[2] = msg.from.size() > 1 ? msg.from[1] : ' ';

    for (int c = 0; c < 3; c++) {
        int ch = static_cast<int>(chars[c]);
        if (selected) ch += INVERT_OFFSET;
        display_.WriteChar(1 + c, row, ch);
    }
}

void ChatScreen::RefreshCursorRows(int old_cursor, int new_cursor) {
    display_.CancelBuffered();
    display_.BeginBuffered();
    if (old_cursor != new_cursor && old_cursor >= 0 && old_cursor < ItemCountOnPage()) {
        WriteSelectionMark(old_cursor, false);
    }
    if (new_cursor >= 0 && new_cursor < ItemCountOnPage()) {
        WriteSelectionMark(new_cursor, true);
    }
    RenderPageIndicators();
}

void ChatScreen::RenderPageIndicators() {
    auto& d = display_;

    if (!messages_.empty()) {
        int global_idx = page_ * ROWS_PER_PAGE + cursor_ + 1;
        int total = static_cast<int>(messages_.size());
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

std::string ChatScreen::FormatRelativeTime(int64_t date) {
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

bool ChatScreen::OnInput(const std::string& key) {
    if (mode_ == CONSENT) {
        // Touch 53 (col 5, row 3) = I CONSENT button
        // Require CONSENT_DELAY_SEC before accepting, so stale/queued
        // inputs from navigation can't auto-accept consent.
        if (key == "53") {
            double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - consent_shown_at_).count();
            if (elapsed < CONSENT_DELAY_SEC) {
                Logger::Debug("Chat consent ignored (too soon: " +
                              std::to_string(elapsed) + "s)");
                return true;
            }
            pda_.GetConfig().SetState("chat.consent", "1");
            Logger::Info("Chat consent given");

            // Switch to feed mode
            mode_ = FEED;
            macro_index = FEED_MACRO;

            // Trigger immediate fetch
            pda_.GetChatClient().FetchMessages();
            messages_ = pda_.GetChatClient().GetMessages();
            if (!messages_.empty()) {
                pda_.MarkChatSeen();
                messages_ = pda_.GetChatClient().GetMessages();
            }

            pda_.StartRender(this);
            return true;
        }
        return false;
    }

    // Feed mode input
    if (messages_.empty()) return false;

    // Joystick: cycle cursor
    if (key == "Joystick") {
        int items = ItemCountOnPage();
        if (items == 0) return true;
        int old_cursor = cursor_;
        cursor_ = (cursor_ + 1) % items;
        RefreshCursorRows(old_cursor, cursor_);
        return true;
    }

    // TR: select message → open detail
    if (key == "TR") {
        int idx = page_ * ROWS_PER_PAGE + cursor_;
        if (idx < static_cast<int>(messages_.size())) {
            pda_.SetSelectedChat(&messages_[idx]);
            pda_.SetPendingNavigate("CHAT_DTL");
        }
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
