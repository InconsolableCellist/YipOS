#include "FuralityScreen.hpp"

#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Glyphs.hpp"
#include "net/FuralityClient.hpp"

#include <cstdio>
#include <ctime>

namespace YipOS {

using namespace Glyphs;

FuralityScreen::FuralityScreen(PDAController& pda) : ListScreen(pda) {
    name = "FUR";
    macro_index = -1;
    refresh_interval = -1;  // static layout, no auto-refresh

    auto* fc = pda_.GetFuralityClient();
    if (fc) {
        // Clear any "imminent event" notification flag — user is engaging
        pda_.MarkFurSeen();
        day_count_cached_ = fc->GetEventInfo().day_count;
        if (!fc->HasData()) {
            // Best-effort blocking fetch on first entry
            fc->FetchAll();
            day_count_cached_ = fc->GetEventInfo().day_count;
        }
    }
}

void FuralityScreen::Render() {
    RenderFrame("FURALITY");
    RenderRows();
    RenderPageIndicators();
    RenderStatusBar();
}

void FuralityScreen::Update() {
    auto* fc = pda_.GetFuralityClient();
    if (!fc) return;
    int new_count = fc->GetEventInfo().day_count;
    if (new_count != day_count_cached_) {
        day_count_cached_ = new_count;
        pda_.StartRender(this);
    }
}

int FuralityScreen::ItemCount() const {
    auto* fc = pda_.GetFuralityClient();
    if (!fc || !fc->HasData()) return 0;
    int days = fc->GetEventInfo().day_count;
    if (days < 1) days = 1;
    return 1 + days + 1;  // ALL + days + MARKED
}

int FuralityScreen::IndexToFurDay(int row) const {
    auto* fc = pda_.GetFuralityClient();
    int days = (fc ? fc->GetEventInfo().day_count : 0);
    if (days < 1) days = 1;
    if (row == 0) return kFurDayAll;
    if (row == 1 + days) return kFurDayMarked;
    return row - 1;
}

void FuralityScreen::RenderEmpty() {
    auto* fc = pda_.GetFuralityClient();
    display_.WriteText(2, 2, "Fetching schedule...");
    if (fc && fc->LastFetch() > 0) {
        display_.WriteText(2, 4, "No events available");
    }
}

void FuralityScreen::RenderRow(int i, bool selected) {
    RenderRowText(i, selected);
}

void FuralityScreen::RenderRowText(int row, bool selected) {
    int global = page_ * ROWS_PER_PAGE + row;
    int fur_day = IndexToFurDay(global);

    auto* fc = pda_.GetFuralityClient();
    if (!fc) return;

    // Layout:
    //   col 1-3 selection mark (inverted)
    //   col 4-25 label (e.g. " ALL EVENTS  Mar 21")
    //   col 26-37 right-aligned " 47 evts" / " 12 ★"

    char label[24] = {0};
    char date[16] = {0};
    int count = 0;

    if (fur_day == kFurDayAll) {
        std::snprintf(label, sizeof(label), "ALL EVENTS");
        count = static_cast<int>(fc->GetEvents().size());
    } else if (fur_day == kFurDayMarked) {
        std::snprintf(label, sizeof(label), "MARKED");
        count = static_cast<int>(fc->MarkedEvents().size());
    } else {
        std::snprintf(label, sizeof(label), "DAY %d", fur_day + 1);
        count = fc->EventCountForDay(fur_day);
        // Compute date for this day
        int64_t day_unix = fc->GetEventInfo().start_unix + int64_t{86400} * fur_day;
        std::time_t t = static_cast<std::time_t>(day_unix);
        std::tm* lt = std::localtime(&t);
        if (lt) std::strftime(date, sizeof(date), "%b %d", lt);
    }

    int row_y = row + 1;  // body rows are 1..6
    if (row_y < 1 || row_y > 6) return;

    auto& d = display_;
    // Clear interior of this row
    for (int c = 1; c < COLS - 1; c++) d.WriteChar(c, row_y, ' ');

    // Selection-mark zone (cols 1..3) handled by WriteSelectionMark, but we
    // also stamp it here so first paint is correct.
    WriteSelectionMark(row, selected);

    int col = 4;
    int label_len = static_cast<int>(std::char_traits<char>::length(label));
    if (label_len > 12) label_len = 12;
    for (int i = 0; i < label_len; i++) d.WriteChar(col + i, row_y, label[i]);

    if (date[0] != '\0') {
        int dlen = static_cast<int>(std::char_traits<char>::length(date));
        d.WriteText(17, row_y, date);
        (void)dlen;
    }

    char right[12];
    if (fur_day == kFurDayMarked) {
        std::snprintf(right, sizeof(right), "%3d", count);
        d.WriteText(31, row_y, right);
        // Star glyph
        d.WriteGlyph(35, row_y, G_HEART);
    } else {
        std::snprintf(right, sizeof(right), "%3d evts", count);
        d.WriteText(30, row_y, right);
    }
}

void FuralityScreen::WriteSelectionMark(int i, bool selected) {
    int row_y = i + 1;
    if (row_y < 1 || row_y > 6) return;
    auto& d = display_;
    int global = page_ * ROWS_PER_PAGE + i;
    int fur_day = IndexToFurDay(global);

    char tag[4];
    if (fur_day == kFurDayAll)         std::snprintf(tag, sizeof(tag), "ALL");
    else if (fur_day == kFurDayMarked) std::snprintf(tag, sizeof(tag), "MRK");
    else                                std::snprintf(tag, sizeof(tag), "D%d ", fur_day + 1);

    for (int c = 0; c < SEL_WIDTH; c++) {
        int ch = static_cast<int>(tag[c]);
        if (selected) ch += INVERT_OFFSET;
        d.WriteChar(1 + c, row_y, ch);
    }
}

bool FuralityScreen::OnSelect(int index) {
    int fur_day = IndexToFurDay(index);
    pda_.SetPendingFurDay(fur_day);
    pda_.SetPendingNavigate("FUR_LIST");
    return true;
}

} // namespace YipOS
