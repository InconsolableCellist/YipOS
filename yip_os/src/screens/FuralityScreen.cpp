#include "FuralityScreen.hpp"

#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Glyphs.hpp"
#include "net/FuralityClient.hpp"

#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <ctime>

namespace YipOS {

using namespace Glyphs;

FuralityScreen::FuralityScreen(PDAController& pda) : ListScreen(pda) {
    name = "FUR";
    macro_index = 51;
    refresh_interval = -1;  // static layout, no auto-refresh
    update_interval = 30.0f; // poll the "next event" countdown every 30s

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
    RecomputeNextEvent();
}

void FuralityScreen::Render() {
    RenderFrame("FURALITY");
    // SEL arrow indicates TR triggers a navigation. Drawn here so the design
    // language matches CC/DM/IMG. (When the macro stamp lands, it will
    // include this glyph and these explicit writes will skip via dirty
    // tracking.)
    display_.WriteGlyph(COLS - 1, 1, G_RIGHT_A);
    RenderRows();
    RenderPageIndicators();
    RenderStatusBar();
}

void FuralityScreen::Update() {
    auto* fc = pda_.GetFuralityClient();
    if (!fc) return;
    int new_count = fc->GetEventInfo().day_count;
    const FurEvent* prev_next = next_event_;
    RecomputeNextEvent();
    if (new_count != day_count_cached_ || next_event_ != prev_next) {
        day_count_cached_ = new_count;
        pda_.StartRender(this);
        return;
    }
    // Same row count, same next event — but the countdown changed. Repaint
    // just the NEXT row in place; dirty-skip in PDADisplay swallows the
    // unchanged cells.
    if (next_event_ && page_ == 0) {
        display_.BeginBuffered();
        RenderRow(0, cursor_ == 0);
    }
}

void FuralityScreen::RecomputeNextEvent() {
    next_event_ = nullptr;
    auto* fc = pda_.GetFuralityClient();
    if (!fc) return;
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    next_event_recomputed_at_ = now;
    int64_t best = INT64_MAX;
    for (auto* ev : fc->MarkedEvents()) {
        if (!ev || ev->start_unix <= now) continue;
        if (ev->start_unix < best) {
            best = ev->start_unix;
            next_event_ = ev;
        }
    }
}

void FuralityScreen::FormatRelative(int64_t seconds, char* out, size_t n) {
    if (seconds < 0) seconds = 0;
    if (seconds < 60) {
        std::snprintf(out, n, "<1m");
    } else if (seconds < 3600) {
        std::snprintf(out, n, "%lldm", static_cast<long long>(seconds / 60));
    } else if (seconds < 86400) {
        long long h = seconds / 3600;
        long long m = (seconds % 3600) / 60;
        std::snprintf(out, n, "%lldh%02lldm", h, m);
    } else {
        long long d = seconds / 86400;
        long long h = (seconds % 86400) / 3600;
        std::snprintf(out, n, "%lldd%lldh", d, h);
    }
}

int FuralityScreen::ItemCount() const {
    auto* fc = pda_.GetFuralityClient();
    if (!fc || !fc->HasData()) return 0;
    int days = fc->GetEventInfo().day_count;
    if (days < 1) days = 1;
    int n = 1 + days + 1;  // ALL + days + MARKED
    if (HasNextRow()) n += 1;
    return n;
}

int FuralityScreen::IndexToFurDay(int row) const {
    auto* fc = pda_.GetFuralityClient();
    int days = (fc ? fc->GetEventInfo().day_count : 0);
    if (days < 1) days = 1;

    int idx = row;
    if (HasNextRow()) {
        if (idx == 0) return kFurDayNext;
        idx -= 1;
    }
    if (idx == 0) return kFurDayAll;
    if (idx == 1 + days) return kFurDayMarked;
    return idx - 1;
}

void FuralityScreen::RenderEmpty() {
    auto* fc = pda_.GetFuralityClient();
    display_.WriteText(2, 2, "Fetching schedule...");
    if (fc && fc->LastFetch() > 0) {
        display_.WriteText(2, 4, "No events available");
    }
}

void FuralityScreen::RenderRow(int row, bool selected) {
    int global = page_ * ROWS_PER_PAGE + row;
    int fur_day = IndexToFurDay(global);

    auto* fc = pda_.GetFuralityClient();
    if (!fc) return;

    int row_y = row + 1;
    if (row_y < 1 || row_y > 6) return;

    // Build the row as a single full-width string so each cell receives its
    // FINAL value in one write — dirty-skip in PDADisplay then suppresses
    // any cell that already matches. Doing a "clear-then-content" two-pass
    // here would re-write every cell twice (once as space, once with the
    // glyph) and the user would see ghosting on every redraw.
    //
    // Layout (cols 1..38, after side rails at 0 and 39):
    //   1..3   selection mark (handled by WriteSelectionMark)
    //   4      gap   (visual separation between SEL tag and entry)
    //   5..    label / NEXT / etc.
    constexpr int kLabelCol  = 5;
    constexpr int kBodyStart = 1;
    constexpr int kBodyEnd   = COLS - 1; // exclusive; col 39 is the side rail
    constexpr int kBodyWidth = kBodyEnd - kBodyStart;

    std::string body(kBodyWidth, ' ');

    // Bake the SEL tag into cols 1..3 of the body buffer so the row paints in
    // a single pass. (WriteSelectionMark is still called for parity with the
    // RefreshCursorRows code path, but dirty-skip absorbs it as a no-op.)
    char sel_tag[4];
    if (fur_day == kFurDayNext)        std::snprintf(sel_tag, sizeof(sel_tag), "NXT");
    else if (fur_day == kFurDayAll)    std::snprintf(sel_tag, sizeof(sel_tag), "ALL");
    else if (fur_day == kFurDayMarked) std::snprintf(sel_tag, sizeof(sel_tag), "MRK");
    else                                std::snprintf(sel_tag, sizeof(sel_tag), "D%d ", fur_day + 1);
    for (int c = 0; c < SEL_WIDTH; c++) body[c] = sel_tag[c];
    auto place = [&](int col, const std::string& s) {
        // col is absolute screen col; translate into body offset.
        int o = col - kBodyStart;
        for (size_t i = 0; i < s.size(); i++) {
            int p = o + static_cast<int>(i);
            if (p < 0 || p >= kBodyWidth) continue;
            body[p] = s[i];
        }
    };

    if (fur_day == kFurDayNext && next_event_) {
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        char rel[12];
        FormatRelative(next_event_->start_unix - now, rel, sizeof(rel));

        place(kLabelCol, "NEXT");
        place(kLabelCol + 5, rel);

        int title_col = kLabelCol + 5 +
                        static_cast<int>(std::char_traits<char>::length(rel)) + 2;
        if (title_col < 17) title_col = 17;
        int title_max = kBodyEnd - title_col;
        std::string title = next_event_->title;
        if (static_cast<int>(title.size()) > title_max && title_max > 1) {
            title = title.substr(0, title_max - 1) + ">";
        } else if (static_cast<int>(title.size()) > title_max) {
            title = title.substr(0, title_max);
        }
        place(title_col, title);
    } else {
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
            int64_t day_unix = fc->GetEventInfo().start_unix + int64_t{86400} * fur_day;
            std::time_t t = static_cast<std::time_t>(day_unix);
            std::tm* lt = std::localtime(&t);
            if (lt) std::strftime(date, sizeof(date), "%b %d", lt);
        }

        place(kLabelCol, label);
        if (date[0] != '\0') place(17, date);

        char right[12];
        if (fur_day == kFurDayMarked) {
            std::snprintf(right, sizeof(right), "%3d", count);
            place(31, right);
            // Heart at col 35
            int p = 35 - kBodyStart;
            if (p >= 0 && p < kBodyWidth) body[p] = static_cast<char>(G_HEART);
        } else {
            std::snprintf(right, sizeof(right), "%3d evts", count);
            place(30, right);
        }
    }

    // Emit the whole row in one pass. SEL_WIDTH cells get the inverted
    // overlay when selected; WriteChar is used (instead of WriteText) because
    // the heart slot stores a glyph index outside the printable range.
    auto& d = display_;
    for (int i = 0; i < kBodyWidth; i++) {
        int idx = static_cast<unsigned char>(body[i]);
        if (selected && i < SEL_WIDTH && idx < INVERT_OFFSET) idx += INVERT_OFFSET;
        d.WriteChar(kBodyStart + i, row_y, idx);
    }
}

void FuralityScreen::WriteSelectionMark(int i, bool selected) {
    int row_y = i + 1;
    if (row_y < 1 || row_y > 6) return;
    auto& d = display_;
    int global = page_ * ROWS_PER_PAGE + i;
    int fur_day = IndexToFurDay(global);

    char tag[4];
    if (fur_day == kFurDayNext)        std::snprintf(tag, sizeof(tag), "NXT");
    else if (fur_day == kFurDayAll)    std::snprintf(tag, sizeof(tag), "ALL");
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
    if (fur_day == kFurDayNext) {
        if (!next_event_) return false;
        pda_.SetSelectedFurEvent(next_event_);
        pda_.SetPendingNavigate("FUR_DTL");
        return true;
    }
    pda_.SetPendingFurDay(fur_day);
    pda_.SetPendingNavigate("FUR_LIST");
    return true;
}

} // namespace YipOS
