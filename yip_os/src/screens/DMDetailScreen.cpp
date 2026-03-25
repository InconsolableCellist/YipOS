#include "DMDetailScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "net/DMClient.hpp"
#include "core/Logger.hpp"
#include <chrono>
#include <ctime>
#include <cstdio>

namespace YipOS {

using namespace Glyphs;

DMDetailScreen::DMDetailScreen(PDAController& pda) : ListScreen(pda) {
    name = "DM_DTL";
    macro_index = -1;
    handle_back = true;

    session_id_ = pda_.GetSelectedDMSession();
    auto* session = pda_.GetDMClient().GetSession(session_id_);
    if (session) {
        peer_name_ = session->peer_name;
        // Mark seen
        if (!session->messages.empty()) {
            pda_.GetDMClient().MarkSessionSeen(session_id_, session->messages[0].date);
        }
    }
    RefreshMessages();
}

void DMDetailScreen::RefreshMessages() {
    messages_.clear();
    auto* session = pda_.GetDMClient().GetSession(session_id_);
    if (session) {
        messages_ = session->messages;  // copy, sorted newest first
    }
}

void DMDetailScreen::RenderEmpty() {
    display_.WriteText(2, 3, "No messages yet");
    display_.WriteText(2, 4, "Send via desktop UI");
}

void DMDetailScreen::Render() {
    // Override title to show peer name
    std::string saved_name = name;
    name = peer_name_.empty() ? "DM" : peer_name_;
    ListScreen::Render();
    name = saved_name;
}

void DMDetailScreen::RenderDynamic() {
    ListScreen::RenderDynamic();
}

void DMDetailScreen::RenderRow(int i, bool selected) {
    auto& d = display_;
    int idx = GlobalIndex(i);
    int row = 1 + i;
    if (idx >= static_cast<int>(messages_.size())) return;

    auto& msg = messages_[idx];
    int max_w = COLS - 2;

    // Format: "You: text" or "PeerName: text" + timestamp right-aligned
    std::string sender = msg.is_mine ? "You" : msg.from_name;
    if (static_cast<int>(sender.size()) > 6)
        sender = sender.substr(0, 6);

    std::string text = msg.text;
    for (auto& ch : text) {
        if (ch == '\n' || ch == '\r') ch = ' ';
    }

    std::string time_str = FormatTimestamp(msg.date);

    std::string line;
    line += sender;
    line += ": ";
    line += text;

    int time_col = max_w - static_cast<int>(time_str.size());
    if (static_cast<int>(line.size()) > time_col - 1)
        line = line.substr(0, time_col - 1);

    while (static_cast<int>(line.size()) < time_col)
        line += ' ';

    for (int c = 0; c < static_cast<int>(line.size()); c++) {
        int ch = static_cast<int>(line[c]);
        if (selected && c < SEL_WIDTH) ch += INVERT_OFFSET;
        d.WriteChar(1 + c, row, ch);
    }

    d.WriteText(1 + time_col, row, time_str);
}

void DMDetailScreen::WriteSelectionMark(int i, bool selected) {
    int idx = GlobalIndex(i);
    int row = 1 + i;
    if (idx >= static_cast<int>(messages_.size())) return;

    auto& msg = messages_[idx];
    std::string sender = msg.is_mine ? "You" : msg.from_name;
    char chars[3];
    chars[0] = sender.size() > 0 ? sender[0] : ' ';
    chars[1] = sender.size() > 1 ? sender[1] : ' ';
    chars[2] = sender.size() > 2 ? sender[2] : ' ';

    for (int c = 0; c < 3; c++) {
        int ch = static_cast<int>(chars[c]);
        if (selected) ch += INVERT_OFFSET;
        display_.WriteChar(1 + c, row, ch);
    }
}

bool DMDetailScreen::OnInput(const std::string& key) {
    return ListScreen::OnInput(key);
}

std::string DMDetailScreen::FormatTimestamp(int64_t date) {
    std::time_t t = static_cast<std::time_t>(date);
    std::tm* lt = std::localtime(&t);
    if (!lt) return "???";

    char buf[8];
    std::strftime(buf, sizeof(buf), "%H:%M", lt);
    return buf;
}

} // namespace YipOS
