#include "VRCXFeedScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Logger.hpp"
#include <cstdio>
#include <algorithm>

namespace YipOS {

using namespace Glyphs;

VRCXFeedScreen::VRCXFeedScreen(PDAController& pda) : Screen(pda) {
    name = "FEED";
    macro_index = 11;
    LoadData();
}

void VRCXFeedScreen::LoadData() {
    auto* vrcx = pda_.GetVRCXData();
    if (vrcx && vrcx->IsOpen()) {
        feed_ = vrcx->GetFeed(200);
    }
}

int VRCXFeedScreen::PageCount() const {
    int n = static_cast<int>(feed_.size());
    if (n == 0) return 1;
    return (n + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
}

int VRCXFeedScreen::ItemCountOnPage() const {
    if (feed_.empty()) return 0;
    int base = page_ * ROWS_PER_PAGE;
    int remaining = static_cast<int>(feed_.size()) - base;
    return std::min(remaining, ROWS_PER_PAGE);
}

void VRCXFeedScreen::Render() {
    RenderFrame("FEED");
    RenderRows();
    RenderPageIndicators();
    RenderStatusBar();
}

void VRCXFeedScreen::RenderDynamic() {
    RenderRows();
    RenderPageIndicators();
    RenderClock();
    RenderCursor();
}

void VRCXFeedScreen::RenderRow(int i, bool selected) {
    auto& d = display_;
    int idx = page_ * ROWS_PER_PAGE + i;
    int row = 1 + i;
    if (idx >= static_cast<int>(feed_.size())) return;

    auto& f = feed_[idx];

    // Format: "+Name          12:34" or "-Name          12:34"
    char indicator = (f.type == "Online") ? '+' : '-';
    std::string time_str = FormatTime(f.created_at);
    int time_len = static_cast<int>(time_str.size());
    int content_width = COLS - 2;
    // indicator(1) + name + time
    int name_max = content_width - time_len - 1;
    std::string line;
    line += indicator;
    std::string dname = f.display_name;
    if (static_cast<int>(dname.size()) > name_max - 1) {
        dname = dname.substr(0, name_max - 1);
    }
    line += dname;

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

void VRCXFeedScreen::RenderRows() {
    auto& d = display_;

    if (feed_.empty()) {
        d.WriteText(2, 3, "No VRCX data");
        return;
    }

    int items = ItemCountOnPage();
    for (int i = 0; i < items; i++) {
        RenderRow(i, i == cursor_);
    }
}

void VRCXFeedScreen::RefreshCursorRows(int old_cursor, int new_cursor) {
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

void VRCXFeedScreen::RenderPageIndicators() {
    auto& d = display_;

    if (!feed_.empty()) {
        int global_idx = page_ * ROWS_PER_PAGE + cursor_ + 1;
        int total = static_cast<int>(feed_.size());
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

std::string VRCXFeedScreen::FormatTime(const std::string& created_at) {
    // created_at format: "2026-03-15 12:34:56"
    // Extract HH:MM
    if (created_at.size() >= 16) {
        return created_at.substr(11, 5);
    }
    return "     ";
}

bool VRCXFeedScreen::OnInput(const std::string& key) {
    if (feed_.empty()) return false;

    // Joystick: cycle cursor down, wrap to top of current page
    if (key == "Joystick") {
        int items = ItemCountOnPage();
        if (items == 0) return true;
        int old_cursor = cursor_;
        cursor_ = (cursor_ + 1) % items;
        RefreshCursorRows(old_cursor, cursor_);
        return true;
    }

    // TR: select highlighted entry → open detail screen
    if (key == "TR") {
        int idx = page_ * ROWS_PER_PAGE + cursor_;
        if (idx < static_cast<int>(feed_.size())) {
            pda_.SetSelectedFeed(&feed_[idx]);
            pda_.SetPendingNavigate("VRCX_FEED_DETAIL");
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
