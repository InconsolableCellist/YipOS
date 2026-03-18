#include "BFIParamScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Config.hpp"
#include <cstdio>
#include <algorithm>
#include <string>
#include <cstring>

namespace YipOS {

using namespace Glyphs;

BFIParamScreen::BFIParamScreen(PDAController& pda) : Screen(pda) {
    name = "BFI_PARAM";
    macro_index = 23;

    // Read which param is currently active
    std::string p = pda.GetConfig().GetState("bfi.param", "2");
    active_idx_ = std::clamp(std::stoi(p), 0, PDAController::BFI_PARAM_COUNT - 1);

    // Find the longest display name for padding
    for (int i = 0; i < PDAController::BFI_PARAM_COUNT; i++) {
        int len = static_cast<int>(std::strlen(PDAController::BFI_PARAMS[i].display_name));
        if (len > max_name_len_) max_name_len_ = len;
    }
}

int BFIParamScreen::PageCount() const {
    int n = PDAController::BFI_PARAM_COUNT;
    return (n + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
}

int BFIParamScreen::ItemCountOnPage() const {
    int base = page_ * ROWS_PER_PAGE;
    int remaining = PDAController::BFI_PARAM_COUNT - base;
    return std::min(remaining, ROWS_PER_PAGE);
}

void BFIParamScreen::RenderRow(int i, bool selected) {
    auto& d = display_;
    int idx = page_ * ROWS_PER_PAGE + i;
    int row = 1 + i;
    if (idx >= PDAController::BFI_PARAM_COUNT) return;

    const char* pname = PDAController::BFI_PARAMS[idx].display_name;
    int name_len = static_cast<int>(std::strlen(pname));

    // Write "+/- Name" padded to max_name_len_ + 2 (prefix chars)
    bool is_active = (idx == active_idx_);

    // Prefix: indicator char + space
    int prefix[2] = { is_active ? '+' : ' ', ' ' };
    for (int c = 0; c < 2; c++) {
        int ch = prefix[c];
        if (selected && c < SEL_WIDTH) ch += INVERT_OFFSET;
        d.WriteChar(1 + c, row, ch);
    }
    // Name chars
    for (int c = 0; c < max_name_len_; c++) {
        int ch = (c < name_len) ? static_cast<int>(pname[c]) : ' ';
        if (selected && (c + 2) < SEL_WIDTH) ch += INVERT_OFFSET;
        d.WriteChar(3 + c, row, ch);
    }
}

void BFIParamScreen::RenderSelPrefix(int i, bool selected) {
    auto& d = display_;
    int idx = page_ * ROWS_PER_PAGE + i;
    int row = 1 + i;
    if (idx >= PDAController::BFI_PARAM_COUNT) return;

    // Only redraw the first SEL_WIDTH chars (the inverted/normal prefix)
    const char* pname = PDAController::BFI_PARAMS[idx].display_name;
    bool is_active = (idx == active_idx_);
    // Chars: col 1='+' or ' ', col 2=' ', col 3=pname[0]
    int chars[SEL_WIDTH] = {is_active ? '+' : ' ', ' ', static_cast<int>(pname[0])};
    for (int c = 0; c < SEL_WIDTH; c++) {
        int ch = chars[c];
        if (selected) ch += INVERT_OFFSET;
        d.WriteChar(1 + c, row, ch);
    }
}

void BFIParamScreen::RenderRows() {
    for (int i = 0; i < ROWS_PER_PAGE; i++) {
        int idx = page_ * ROWS_PER_PAGE + i;
        if (idx >= PDAController::BFI_PARAM_COUNT) break;
        RenderRow(i, i == cursor_);
    }
}

void BFIParamScreen::RefreshCursorRows(int old_cursor, int new_cursor) {
    display_.CancelBuffered();
    display_.BeginBuffered();
    // Only redraw the SEL_WIDTH prefix for old and new cursor rows
    if (old_cursor != new_cursor && old_cursor >= 0 && old_cursor < ItemCountOnPage()) {
        RenderSelPrefix(old_cursor, false);
    }
    if (new_cursor >= 0 && new_cursor < ItemCountOnPage()) {
        RenderSelPrefix(new_cursor, true);
    }
    RenderPageIndicators();
}

void BFIParamScreen::RenderPageIndicators() {
    auto& d = display_;
    int global_idx = page_ * ROWS_PER_PAGE + cursor_ + 1;
    int total = PDAController::BFI_PARAM_COUNT;
    char pos[12];
    std::snprintf(pos, sizeof(pos), "%d/%d", global_idx, total);
    d.WriteText(5, 7, pos);

    if (PageCount() <= 1) return;
    if (page_ > 0) {
        d.WriteGlyph(0, 3, G_UP);
    }
    if (page_ < PageCount() - 1) {
        d.WriteGlyph(0, 5, G_DOWN);
    }
}

void BFIParamScreen::Render() {
    RenderFrame("BFI PARAM");

    if (PDAController::BFI_PARAM_COUNT > 0) {
        RenderRows();
    }

    RenderStatusBar();
}

void BFIParamScreen::RenderDynamic() {
    if (PDAController::BFI_PARAM_COUNT > 0) {
        RenderRows();
        RenderPageIndicators();
    }
    RenderClock();
    RenderCursor();
}

bool BFIParamScreen::OnInput(const std::string& key) {
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
        if (idx < PDAController::BFI_PARAM_COUNT) {
            int old_active = active_idx_;
            active_idx_ = idx;
            pda_.GetConfig().SetState("bfi.param", std::to_string(idx));

            // Redraw old and new active rows to move the "+" indicator
            display_.CancelBuffered();
            display_.BeginBuffered();
            int old_row_on_page = old_active - page_ * ROWS_PER_PAGE;
            if (old_row_on_page >= 0 && old_row_on_page < ROWS_PER_PAGE) {
                RenderRow(old_row_on_page, old_row_on_page == cursor_);
            }
            RenderRow(cursor_, true);

            pda_.SetPendingNavigate("__POP__");
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
