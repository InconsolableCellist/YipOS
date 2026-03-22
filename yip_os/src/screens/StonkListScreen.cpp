#include "StonkListScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "net/StockClient.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include <cstdio>
#include <algorithm>

namespace YipOS {

using namespace Glyphs;

StonkListScreen::StonkListScreen(PDAController& pda) : Screen(pda) {
    name = "STONK_LIST";
    macro_index = 32;

    auto* client = pda_.GetStockClient();
    if (client) {
        symbols_ = client->GetSymbols();
    }
}

int StonkListScreen::PageCount() const {
    int n = static_cast<int>(symbols_.size());
    if (n == 0) return 1;
    return (n + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
}

int StonkListScreen::ItemCountOnPage() const {
    if (symbols_.empty()) return 0;
    int base = page_ * ROWS_PER_PAGE;
    int remaining = static_cast<int>(symbols_.size()) - base;
    return std::min(remaining, ROWS_PER_PAGE);
}

void StonkListScreen::Render() {
    RenderFrame("STONK");
    RenderRows();
    RenderPageIndicators();
    RenderStatusBar();
}

void StonkListScreen::RenderDynamic() {
    RenderRows();
    RenderPageIndicators();
    RenderClock();
    RenderCursor();
}

void StonkListScreen::RenderRow(int i, bool selected) {
    auto& d = display_;
    int idx = page_ * ROWS_PER_PAGE + i;
    int row = 1 + i;
    if (idx >= static_cast<int>(symbols_.size())) return;

    const std::string& sym = symbols_[idx];
    std::string current_sel = pda_.GetConfig().GetState("stonk.selected", "DOGE");
    bool is_active = (sym == current_sel);

    // Symbol name with active indicator
    std::string line = sym;
    if (is_active) line += " *";

    static constexpr int SEL_WIDTH = 3;
    for (int c = 0; c < static_cast<int>(line.size()) && c < COLS - 2; c++) {
        int ch = static_cast<int>(line[c]);
        if (selected && c < SEL_WIDTH) ch += INVERT_OFFSET;
        d.WriteChar(1 + c, row, ch);
    }
}

void StonkListScreen::RenderRows() {
    auto& d = display_;

    if (symbols_.empty()) {
        d.WriteText(2, 3, "No symbols configured");
        return;
    }

    int items = ItemCountOnPage();
    for (int i = 0; i < items; i++) {
        RenderRow(i, i == cursor_);
    }
}

void StonkListScreen::WriteSelectionMark(int i, bool selected) {
    int idx = page_ * ROWS_PER_PAGE + i;
    int row = 1 + i;
    if (idx >= static_cast<int>(symbols_.size())) return;

    const std::string& sym = symbols_[idx];
    for (int c = 0; c < 3 && c < static_cast<int>(sym.size()); c++) {
        int ch = static_cast<int>(sym[c]);
        if (selected) ch += INVERT_OFFSET;
        display_.WriteChar(1 + c, row, ch);
    }
}

void StonkListScreen::RefreshCursorRows(int old_cursor, int new_cursor) {
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

void StonkListScreen::RenderPageIndicators() {
    auto& d = display_;

    if (!symbols_.empty()) {
        int global_idx = page_ * ROWS_PER_PAGE + cursor_ + 1;
        int total = static_cast<int>(symbols_.size());
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

bool StonkListScreen::OnInput(const std::string& key) {
    if (symbols_.empty()) return false;

    // Joystick: cycle cursor
    if (key == "Joystick") {
        int items = ItemCountOnPage();
        if (items == 0) return true;
        int old_cursor = cursor_;
        cursor_ = (cursor_ + 1) % items;
        RefreshCursorRows(old_cursor, cursor_);
        return true;
    }

    // TR: select symbol → save and pop back to StonkScreen
    if (key == "TR") {
        int idx = page_ * ROWS_PER_PAGE + cursor_;
        if (idx < static_cast<int>(symbols_.size())) {
            pda_.GetConfig().SetState("stonk.selected", symbols_[idx]);
            Logger::Info("STONK: selected " + symbols_[idx]);
            pda_.SetPendingNavigate("__POP__");
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
