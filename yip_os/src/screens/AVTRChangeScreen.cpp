#include "AVTRChangeScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Logger.hpp"
#include <cstdio>
#include <algorithm>

namespace YipOS {

using namespace Glyphs;

AVTRChangeScreen::AVTRChangeScreen(PDAController& pda) : Screen(pda) {
    name = "CHANGE";
    macro_index = 17;
    LoadData();
}

void AVTRChangeScreen::LoadData() {
    auto* data = pda_.GetAvatarData();
    if (data) {
        avatars_ = data->GetAvatars();
    }
}

int AVTRChangeScreen::PageCount() const {
    int n = static_cast<int>(avatars_.size());
    if (n == 0) return 1;
    return (n + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
}

int AVTRChangeScreen::ItemCountOnPage() const {
    if (avatars_.empty()) return 0;
    int base = page_ * ROWS_PER_PAGE;
    int remaining = static_cast<int>(avatars_.size()) - base;
    return std::min(remaining, ROWS_PER_PAGE);
}

void AVTRChangeScreen::RenderRow(int i, bool selected) {
    auto& d = display_;
    int idx = page_ * ROWS_PER_PAGE + i;
    int row = 1 + i;
    if (idx >= static_cast<int>(avatars_.size())) return;

    auto& a = avatars_[idx];

    // Format: "AvatarName" — first 3 chars inverted for selection highlight
    int content_width = COLS - 2;
    std::string dname = a.name;
    if (static_cast<int>(dname.size()) > content_width) {
        dname = dname.substr(0, content_width);
    }

    for (int c = 0; c < static_cast<int>(dname.size()); c++) {
        int ch = static_cast<int>(dname[c]);
        if (selected && c < SEL_WIDTH) ch += INVERT_OFFSET;
        d.WriteChar(1 + c, row, ch);
    }
}

void AVTRChangeScreen::RenderRows() {
    for (int i = 0; i < ROWS_PER_PAGE; i++) {
        int idx = page_ * ROWS_PER_PAGE + i;
        if (idx >= static_cast<int>(avatars_.size())) break;
        RenderRow(i, i == cursor_);
    }
}

void AVTRChangeScreen::RefreshCursorRows(int old_cursor, int new_cursor) {
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

void AVTRChangeScreen::RenderPageIndicators() {
    auto& d = display_;
    if (!avatars_.empty()) {
        int global_idx = page_ * ROWS_PER_PAGE + cursor_ + 1;
        int total = static_cast<int>(avatars_.size());
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

void AVTRChangeScreen::Render() {
    RenderFrame("CHANGE");

    if (avatars_.empty()) {
        display_.WriteText(2, 2, "No avatars found");
        display_.WriteText(2, 3, "Set VRC OSC path");
        display_.WriteText(2, 4, "in Config tab");
    } else {
        RenderRows();
    }

    RenderStatusBar();
}

void AVTRChangeScreen::RenderDynamic() {
    if (!avatars_.empty()) {
        RenderRows();
        RenderPageIndicators();
    }
    RenderClock();
    RenderCursor();
}

bool AVTRChangeScreen::OnInput(const std::string& key) {
    if (avatars_.empty()) return false;

    if (key == "Joystick") {
        int items = ItemCountOnPage();
        if (items == 0) return true;
        int old_cursor = cursor_;
        cursor_ = (cursor_ + 1) % items;
        RefreshCursorRows(old_cursor, cursor_);
        return true;
    }

    if (key == "TR") {
        int idx = page_ * ROWS_PER_PAGE + cursor_;
        if (idx < static_cast<int>(avatars_.size())) {
            pda_.SetSelectedAvatar(&avatars_[idx]);
            pda_.SetPendingNavigate("AVTR_DETAIL");
        }
        return true;
    }

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
