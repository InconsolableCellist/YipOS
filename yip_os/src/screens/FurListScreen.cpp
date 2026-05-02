#include "FurListScreen.hpp"

#include "FuralityScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Glyphs.hpp"
#include "net/FuralityClient.hpp"

#include <cstdio>
#include <ctime>

namespace YipOS {

using namespace Glyphs;

FurListScreen::FurListScreen(PDAController& pda) : ListScreen(pda) {
    name = "FUR_LIST";
    macro_index = -1;
    refresh_interval = -1;

    fur_day_ = pda_.GetPendingFurDay();
    SyncEvents();
    pda_.MarkFurSeen();
}

void FurListScreen::SyncEvents() {
    auto* fc = pda_.GetFuralityClient();
    events_.clear();
    if (!fc) return;
    if (fur_day_ == FuralityScreen::kFurDayMarked) {
        events_ = fc->MarkedEvents();
    } else if (fur_day_ == FuralityScreen::kFurDayAll) {
        events_ = fc->AllEvents();
    } else {
        events_ = fc->EventsForDay(fur_day_);
    }
}

std::string FurListScreen::TitleForDay() const {
    if (fur_day_ == FuralityScreen::kFurDayAll) return "ALL EVENTS";
    if (fur_day_ == FuralityScreen::kFurDayMarked) return "MARKED";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "DAY %d", fur_day_ + 1);
    return buf;
}

void FurListScreen::Render() {
    SyncEvents();
    RenderFrame(TitleForDay());
    RenderRows();
    RenderPageIndicators();
    RenderStatusBar();
}

int FurListScreen::ItemCount() const {
    return static_cast<int>(events_.size());
}

void FurListScreen::RenderEmpty() {
    if (fur_day_ == FuralityScreen::kFurDayMarked) {
        display_.WriteText(2, 3, "No marked events");
        display_.WriteText(2, 5, "TR on event to mark");
    } else {
        display_.WriteText(2, 3, "No events for this day");
    }
}

void FurListScreen::RenderRow(int i, bool selected) {
    int global = page_ * ROWS_PER_PAGE + i;
    if (global < 0 || global >= static_cast<int>(events_.size())) return;
    const FurEvent* ev = events_[global];
    if (!ev) return;

    int row_y = i + 1;
    if (row_y < 1 || row_y > 6) return;

    auto& d = display_;
    auto* fc = pda_.GetFuralityClient();
    bool marked = fc && fc->IsMarked(ev->id);

    // Clear interior
    for (int c = 1; c < COLS - 1; c++) d.WriteChar(c, row_y, ' ');

    // Cols 1-5: HH:MM (selection-marked area is the first 3 chars)
    char hhmm[8] = "--:--";
    if (ev->start_unix > 0) {
        std::time_t t = static_cast<std::time_t>(ev->start_unix);
        std::tm* lt = std::localtime(&t);
        if (lt) std::strftime(hhmm, sizeof(hhmm), "%H:%M", lt);
    }
    for (int c = 0; c < 5; c++) {
        int ch = static_cast<int>(hhmm[c]);
        if (selected && c < SEL_WIDTH) ch += INVERT_OFFSET;
        d.WriteChar(1 + c, row_y, ch);
    }

    // Col 7: heart glyph if marked
    if (marked) d.WriteGlyph(7, row_y, G_HEART);

    // Cols 9-37: title (truncated)
    int title_col = 9;
    int title_max = COLS - 1 - title_col;  // 30
    std::string title = ev->title.empty() ? std::string("(untitled)") : ev->title;
    if (static_cast<int>(title.size()) > title_max) {
        if (title_max > 3) title = title.substr(0, title_max - 3) + "...";
        else title = title.substr(0, title_max);
    }
    for (size_t c = 0; c < title.size(); c++) {
        char ch = title[c];
        if (ch < 32 || ch > 126) ch = '?';
        d.WriteChar(title_col + static_cast<int>(c), row_y, static_cast<int>(ch));
    }
}

void FurListScreen::WriteSelectionMark(int i, bool selected) {
    int global = page_ * ROWS_PER_PAGE + i;
    if (global < 0 || global >= static_cast<int>(events_.size())) return;
    const FurEvent* ev = events_[global];
    if (!ev) return;

    int row_y = i + 1;
    if (row_y < 1 || row_y > 6) return;

    char hhmm[8] = "--:--";
    if (ev->start_unix > 0) {
        std::time_t t = static_cast<std::time_t>(ev->start_unix);
        std::tm* lt = std::localtime(&t);
        if (lt) std::strftime(hhmm, sizeof(hhmm), "%H:%M", lt);
    }
    auto& d = display_;
    for (int c = 0; c < SEL_WIDTH; c++) {
        int ch = static_cast<int>(hhmm[c]);
        if (selected) ch += INVERT_OFFSET;
        d.WriteChar(1 + c, row_y, ch);
    }
}

bool FurListScreen::OnSelect(int index) {
    if (index < 0 || index >= static_cast<int>(events_.size())) return false;
    pda_.SetSelectedFurEvent(events_[index]);
    pda_.SetPendingNavigate("FUR_DTL");
    return true;
}

} // namespace YipOS
