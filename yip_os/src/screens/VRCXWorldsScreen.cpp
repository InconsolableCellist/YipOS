#include "VRCXWorldsScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Logger.hpp"
#include <cstdio>
#include <algorithm>

namespace YipOS {

using namespace Glyphs;

VRCXWorldsScreen::VRCXWorldsScreen(PDAController& pda) : Screen(pda) {
    name = "WORLDS";
    macro_index = 9;
    LoadData();
}

void VRCXWorldsScreen::LoadData() {
    auto* vrcx = pda_.GetVRCXData();
    if (vrcx && vrcx->IsOpen()) {
        worlds_ = vrcx->GetWorlds(300);
    }
}

int VRCXWorldsScreen::PageCount() const {
    int n = static_cast<int>(worlds_.size());
    if (n == 0) return 1;
    return (n + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
}

int VRCXWorldsScreen::ItemCountOnPage() const {
    if (worlds_.empty()) return 0;
    int base = page_ * ROWS_PER_PAGE;
    int remaining = static_cast<int>(worlds_.size()) - base;
    return std::min(remaining, ROWS_PER_PAGE);
}

void VRCXWorldsScreen::Render() {
    RenderFrame("WORLDS");
    RenderRows();
    RenderPageIndicators();
    RenderStatusBar();
}

void VRCXWorldsScreen::RenderDynamic() {
    RenderRows();
    RenderPageIndicators();
    RenderClock();
    RenderCursor();
}

void VRCXWorldsScreen::RenderRow(int i, bool selected) {
    auto& d = display_;
    int idx = page_ * ROWS_PER_PAGE + i;
    int row = 1 + i;
    if (idx >= static_cast<int>(worlds_.size())) return;

    auto& w = worlds_[idx];

    std::string dur = FormatDuration(w.time_seconds);
    int dur_len = static_cast<int>(dur.size());
    int content_width = COLS - 2;
    int name_max = content_width - dur_len - 1;
    std::string wname = w.world_name;
    if (static_cast<int>(wname.size()) > name_max) {
        wname = wname.substr(0, name_max);
    }

    // First 3 chars inverted = selection indicator, rest always plain
    static constexpr int SEL_WIDTH = 3;

    // Write name (first 3 chars may be inverted)
    for (int c = 0; c < static_cast<int>(wname.size()); c++) {
        int ch = static_cast<int>(wname[c]);
        if (selected && c < SEL_WIDTH) ch += INVERT_OFFSET;
        d.WriteChar(1 + c, row, ch);
    }

    // Duration right-justified
    int dur_col = COLS - 1 - dur_len;
    d.WriteText(dur_col, row, dur);
}

void VRCXWorldsScreen::RenderRows() {
    auto& d = display_;

    if (worlds_.empty()) {
        d.WriteText(2, 3, "No VRCX data");
        return;
    }

    int items = ItemCountOnPage();
    for (int i = 0; i < items; i++) {
        RenderRow(i, i == cursor_);
    }
}

void VRCXWorldsScreen::RefreshCursorRows(int old_cursor, int new_cursor) {
    // Only update the two affected rows without a full re-render
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

void VRCXWorldsScreen::RenderPageIndicators() {
    auto& d = display_;

    // Always show cursor position indicator
    if (!worlds_.empty()) {
        int global_idx = page_ * ROWS_PER_PAGE + cursor_ + 1;
        int total = static_cast<int>(worlds_.size());
        char pos[12];
        std::snprintf(pos, sizeof(pos), "%d/%d", global_idx, total);
        d.WriteText(3, 7, pos);
    }

    if (PageCount() <= 1) return;

    if (page_ > 0) {
        d.WriteGlyph(0, 3, G_UP);
    }
    if (page_ < PageCount() - 1) {
        d.WriteGlyph(0, 5, G_DOWN);
    }
}

std::string VRCXWorldsScreen::FormatDuration(int64_t seconds) {
    if (seconds <= 0) return "  <1m";
    if (seconds < 3600) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%3ldm", static_cast<long>(seconds / 60));
        return buf;
    }
    int hrs = static_cast<int>(seconds / 3600);
    int mins = static_cast<int>((seconds % 3600) / 60);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d:%02d", hrs, mins);
    return buf;
}

bool VRCXWorldsScreen::OnInput(const std::string& key) {
    if (worlds_.empty()) return false;

    // Joystick: cycle cursor down, wrap to top of current page
    if (key == "Joystick") {
        int items = ItemCountOnPage();
        if (items == 0) return true;
        int old_cursor = cursor_;
        cursor_ = (cursor_ + 1) % items;
        RefreshCursorRows(old_cursor, cursor_);
        return true;
    }

    // TR: select highlighted world → open detail screen
    if (key == "TR") {
        int idx = page_ * ROWS_PER_PAGE + cursor_;
        if (idx < static_cast<int>(worlds_.size())) {
            Logger::Info("WORLDS: selected #" + std::to_string(idx) + " " + worlds_[idx].world_name);
            pda_.SetSelectedWorld(&worlds_[idx]);
            pda_.SetPendingNavigate("VRCX_WORLD_DETAIL");
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
