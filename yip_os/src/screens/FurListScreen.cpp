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
    macro_index = 52;
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
    // SEL arrow: TR opens the selected event detail
    display_.WriteGlyph(0, 1, G_LEFT_A);
    display_.WriteGlyph(COLS - 1, 1, G_RIGHT_A);
    RenderRows();
    RenderPageIndicators();
    RenderStatusBar();
}

void FurListScreen::RenderDynamic() {
    // The macro provides "ALL EVENTS" as the baked title; overwrite it with
    // the per-day name. RenderFrame's repeats over the side rails are no-ops
    // thanks to dirty-skip in PDADisplay, so this only repaints the title
    // cells that actually differ.
    SyncEvents();
    RenderFrame(TitleForDay());
    ListScreen::RenderDynamic();
}

int FurListScreen::ItemCount() const {
    return static_cast<int>(events_.size());
}

void FurListScreen::RenderEmpty() {
    if (fur_day_ == FuralityScreen::kFurDayMarked) {
        display_.WriteText(2, 3, "No marked events");
        // The detail screen has the SEL arrow indicator; users learn there
        // that SEL toggles the heart. No need for an inline hint here.
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

    auto* fc = pda_.GetFuralityClient();
    bool marked = fc && fc->IsMarked(ev->id);

    // Single-pass full-width row build — see FuralityScreen::RenderRow for
    // the rationale (avoids the clear-then-content double-write).
    constexpr int kBodyStart = 1;
    constexpr int kBodyEnd   = COLS - 1;
    constexpr int kBodyWidth = kBodyEnd - kBodyStart;

    std::string body(kBodyWidth, ' ');

    // Cols 1..5: HH:MM (selection-marked area is the first 3 chars; that
    // overlay is applied below).
    char hhmm[8] = "--:--";
    if (ev->start_unix > 0) {
        std::time_t t = static_cast<std::time_t>(ev->start_unix);
        std::tm* lt = std::localtime(&t);
        if (lt) std::strftime(hhmm, sizeof(hhmm), "%H:%M", lt);
    }
    for (int c = 0; c < 5; c++) {
        body[c] = hhmm[c];  // body[0] = col 1 (kBodyStart)
    }

    // Col 7: heart glyph if marked
    if (marked) {
        body[7 - kBodyStart] = static_cast<char>(G_HEART);
    }

    // Col 9: title (with implicit gap at col 8)
    int title_col = 9;
    int title_max = kBodyEnd - title_col;
    std::string title = ev->title.empty() ? std::string("(untitled)") : ev->title;
    if (static_cast<int>(title.size()) > title_max) {
        if (title_max > 3) title = title.substr(0, title_max - 3) + "...";
        else title = title.substr(0, title_max);
    }
    for (size_t c = 0; c < title.size(); c++) {
        char ch = title[c];
        if (ch < 32 || ch > 126) ch = '?';
        body[(title_col - kBodyStart) + static_cast<int>(c)] = ch;
    }

    // Only write non-space cells (screen is already cleared). SEL_WIDTH
    // cells always write for the inverted overlay when selected.
    auto& d = display_;
    for (int c = 0; c < kBodyWidth; c++) {
        unsigned char ch = static_cast<unsigned char>(body[c]);
        if (selected && c < SEL_WIDTH) {
            int idx = static_cast<int>(ch);
            if (idx < INVERT_OFFSET) idx += INVERT_OFFSET;
            d.WriteChar(kBodyStart + c, row_y, idx);
        } else if (ch != ' ') {
            d.WriteChar(kBodyStart + c, row_y, ch);
        }
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
