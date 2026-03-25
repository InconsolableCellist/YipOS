#include "DMScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "net/DMClient.hpp"
#include "core/Logger.hpp"
#include <chrono>
#include <cstdio>

namespace YipOS {

using namespace Glyphs;

DMScreen::DMScreen(PDAController& pda) : ListScreen(pda) {
    name = "DM";
    macro_index = -1;  // no macro yet — character-by-character render
    RefreshSessions();

    // Mark all DM as seen when entering the screen
    pda_.MarkDMSeen();
}

void DMScreen::RefreshSessions() {
    sessions_.clear();
    auto& client = pda_.GetDMClient();
    for (auto& s : client.GetSessions()) {
        sessions_.push_back(&s);
    }
}

void DMScreen::RenderEmpty() {
    display_.WriteText(2, 2, "No conversations");
    display_.WriteText(2, 4, "Use PAIR to add a");
    display_.WriteText(2, 5, "contact");
}

void DMScreen::Render() {
    ListScreen::Render();
    RenderPairButton();
}

void DMScreen::RenderDynamic() {
    ListScreen::RenderDynamic();
}

void DMScreen::RenderPairButton() {
    // Bottom-left of content area: "PAIR" as inverted touchable text
    // Row 6, col 1 — mirrors the pattern of touchable labels
    std::string label = "PAIR";
    for (int i = 0; i < static_cast<int>(label.size()); i++) {
        display_.WriteChar(1 + i, 6, static_cast<int>(label[i]) + INVERT_OFFSET);
    }
}

void DMScreen::RenderRow(int i, bool selected) {
    auto& d = display_;
    int idx = GlobalIndex(i);
    int row = 1 + i;
    if (idx >= static_cast<int>(sessions_.size())) return;

    auto* session = sessions_[idx];

    char indicator = session->has_unseen ? '*' : ' ';

    // Peer name (truncated to 8 chars)
    std::string peer = session->peer_name;
    if (static_cast<int>(peer.size()) > 8)
        peer = peer.substr(0, 8);

    // Last message preview
    std::string preview;
    int64_t last_date = 0;
    if (!session->messages.empty()) {
        auto& last = session->messages[0]; // newest first
        preview = last.text;
        last_date = last.date;
        for (auto& ch : preview) {
            if (ch == '\n' || ch == '\r') ch = ' ';
        }
    }

    std::string time_str = last_date > 0 ? FormatRelativeTime(last_date) : "";
    int content_width = COLS - 2;

    // Format: *PeerName: preview text          <1m
    std::string line;
    line += indicator;
    line += peer;
    if (!preview.empty()) {
        line += ": ";
        int remaining = content_width - static_cast<int>(line.size()) -
                        static_cast<int>(time_str.size()) - 1;
        if (remaining > 3) {
            if (static_cast<int>(preview.size()) > remaining)
                preview = preview.substr(0, remaining - 3) + "...";
            line += preview;
        }
    }

    int time_col = content_width - static_cast<int>(time_str.size());
    while (static_cast<int>(line.size()) < time_col)
        line += ' ';
    if (static_cast<int>(line.size()) > time_col)
        line = line.substr(0, time_col);

    for (int c = 0; c < static_cast<int>(line.size()); c++) {
        int ch = static_cast<int>(line[c]);
        if (selected && c < SEL_WIDTH) ch += INVERT_OFFSET;
        d.WriteChar(1 + c, row, ch);
    }

    d.WriteText(1 + time_col, row, time_str);
}

void DMScreen::WriteSelectionMark(int i, bool selected) {
    int idx = GlobalIndex(i);
    int row = 1 + i;
    if (idx >= static_cast<int>(sessions_.size())) return;

    auto* session = sessions_[idx];
    char chars[3];
    chars[0] = session->has_unseen ? '*' : ' ';
    chars[1] = session->peer_name.size() > 0 ? session->peer_name[0] : ' ';
    chars[2] = session->peer_name.size() > 1 ? session->peer_name[1] : ' ';

    for (int c = 0; c < 3; c++) {
        int ch = static_cast<int>(chars[c]);
        if (selected) ch += INVERT_OFFSET;
        display_.WriteChar(1 + c, row, ch);
    }
}

bool DMScreen::OnSelect(int index) {
    if (index < static_cast<int>(sessions_.size())) {
        pda_.SetSelectedDMSession(sessions_[index]->session_id);
        pda_.SetPendingNavigate("DM_DTL");
    }
    return true;
}

bool DMScreen::OnInput(const std::string& key) {
    // Touch contact for PAIR button (bottom-left area = zone row 6, col range)
    // Contact 53 = TR button, but for PAIR we use a touch zone
    // Row 6 maps to ZONE_ROWS[2] = 6, leftmost tile center = TILE_CENTERS[0] = 4
    if (key == "46") {
        // BL contact — navigate to pairing screen
        pda_.SetPendingNavigate("DM_PAIR");
        return true;
    }

    return ListScreen::OnInput(key);
}

std::string DMScreen::FormatRelativeTime(int64_t date) {
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

} // namespace YipOS
