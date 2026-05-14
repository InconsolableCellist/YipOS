#include "FurDetailScreen.hpp"

#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Config.hpp"
#include "core/Glyphs.hpp"
#include "net/FuralityClient.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

namespace YipOS {

using namespace Glyphs;

FurDetailScreen::FurDetailScreen(PDAController& pda) : Screen(pda) {
    name = "FUR_DTL";
    macro_index = 53;
    refresh_interval = -1;
    ev_ = pda_.GetSelectedFurEvent();
    BuildDescriptionLines();
}

void FurDetailScreen::Render() {
    RenderFrame("EVENT");
    // Back arrow + SEL arrow (SEL toggles the heart mark)
    display_.WriteGlyph(0, 1, G_LEFT_A);
    display_.WriteGlyph(COLS - 1, 1, G_RIGHT_A);
    RenderHeader();
    RenderDescription();
    RenderMarkIndicator();
    RenderPageIndicator();
    RenderStatusBar();
}

void FurDetailScreen::RenderDynamic() {
    // After a macro stamp, the frame and back/SEL arrows are already on the
    // RT. Just paint the dynamic content (dirty-skip swallows any redundant
    // cells if RenderHeader writes spaces over already-blank cells).
    RenderHeader();
    RenderDescription();
    RenderMarkIndicator();
    RenderPageIndicator();
    RenderClock();
    RenderCursor();
}

std::string FurDetailScreen::FormatTimeRange() const {
    if (!ev_) return {};
    if (ev_->start_unix <= 0) return {};
    char buf[32];
    std::time_t s = static_cast<std::time_t>(ev_->start_unix);
    std::tm* slt = std::localtime(&s);
    if (!slt) return {};
    std::strftime(buf, sizeof(buf), "%a %b %d %H:%M", slt);
    std::string out = buf;
    if (ev_->end_unix > ev_->start_unix) {
        std::time_t e = static_cast<std::time_t>(ev_->end_unix);
        std::tm* elt = std::localtime(&e);
        if (elt) {
            char eb[16];
            std::strftime(eb, sizeof(eb), "%H:%M", elt);
            out += "-";
            out += eb;
        }
    }
    return out;
}

void FurDetailScreen::BuildDescriptionLines() {
    desc_lines_.clear();
    if (!ev_) return;
    const int max_w = COLS - 2;  // 38

    // Sanitize: strip non-printable, collapse whitespace runs to single spaces.
    std::string raw;
    raw.reserve(ev_->description.size());
    bool prev_space = true;
    for (char c : ev_->description) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || u > 126) c = ' ';
        if (c == ' ') {
            if (prev_space) continue;
            prev_space = true;
        } else {
            prev_space = false;
        }
        raw.push_back(c);
    }
    while (!raw.empty() && raw.back() == ' ') raw.pop_back();

    // Greedy word-wrap at the last space within max_w.
    size_t i = 0;
    while (i < raw.size()) {
        size_t take = std::min<size_t>(max_w, raw.size() - i);
        if (i + take < raw.size()) {
            size_t brk = raw.rfind(' ', i + take);
            if (brk != std::string::npos && brk > i) {
                take = brk - i;
            }
        }
        desc_lines_.emplace_back(raw.substr(i, take));
        i += take;
        while (i < raw.size() && raw[i] == ' ') i++;
    }
}

int FurDetailScreen::PageCount() const {
    int total = static_cast<int>(desc_lines_.size());
    if (total <= FirstPageDescRows()) return 1;
    int rest = total - FirstPageDescRows();
    int more_pages = (rest + LaterPageDescRows() - 1) / LaterPageDescRows();
    return 1 + more_pages;
}

int FurDetailScreen::LinesOnPage(int page) const {
    return page == 0 ? FirstPageDescRows() : LaterPageDescRows();
}

int FurDetailScreen::LineOffsetForPage(int page) const {
    if (page <= 0) return 0;
    return FirstPageDescRows() + (page - 1) * LaterPageDescRows();
}

void FurDetailScreen::RenderHeader() {
    auto& d = display_;
    int max_w = COLS - 2;

    if (!ev_) {
        d.WriteText(2, 3, "No event selected");
        return;
    }

    if (page_ != 0) return;

    std::string title = ev_->title.empty() ? std::string("(untitled)") : ev_->title;
    // Leave 1 col on the right for the SEL arrow at (COLS-1, 1).
    int title_max = max_w - 1;
    if (static_cast<int>(title.size()) > title_max) title = title.substr(0, title_max);
    d.WriteText(1, 1, title);

    std::string when = FormatTimeRange();
    if (!when.empty()) {
        if (static_cast<int>(when.size()) > max_w) when = when.substr(0, max_w);
        d.WriteText(1, 2, when);
    }

    std::string sub;
    if (!ev_->host.empty()) sub = ev_->host;
    if (!ev_->location.empty()) {
        if (!sub.empty()) sub += " @ ";
        sub += ev_->location;
    }
    if (!sub.empty()) {
        if (static_cast<int>(sub.size()) > max_w) sub = sub.substr(0, max_w);
        d.WriteText(1, 3, sub);
    }
}

void FurDetailScreen::RenderDescription() {
    auto& d = display_;
    if (!ev_) return;

    int first_row = (page_ == 0) ? 4 : 1;
    int last_row = 6;
    int row_count = last_row - first_row + 1;

    int line_offset = LineOffsetForPage(page_);
    int total = static_cast<int>(desc_lines_.size());

    for (int r = 0; r < row_count; r++) {
        int line_idx = line_offset + r;
        int row_y = first_row + r;
        if (line_idx >= total) continue;
        d.WriteText(1, row_y, desc_lines_[line_idx]);
    }
}

void FurDetailScreen::RenderMarkIndicator() {
    auto& d = display_;
    auto* fc = pda_.GetFuralityClient();
    bool marked = (ev_ && fc && fc->IsMarked(ev_->id));
    // Heart at row 6 col 1 when marked (left of the page indicator).
    d.WriteChar(1, 6, marked ? G_HEART : ' ');
}

void FurDetailScreen::RenderPageIndicator() {
    int total = PageCount();
    if (total <= 1) return;
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%d/%d", page_ + 1, total);
    int len = static_cast<int>(std::char_traits<char>::length(buf));
    int col = COLS - 1 - len;
    // Page-indicator goes at row 6 right; the heart is at col 1.
    display_.WriteText(col, 6, buf);
    if (page_ > 0) display_.WriteGlyph(col - 2, 6, G_LEFT_A);
    if (page_ < total - 1) display_.WriteGlyph(COLS - 1, 6, G_RIGHT_A);
}

bool FurDetailScreen::OnInput(const std::string& key) {
    if (!ev_) return false;
    auto* fc = pda_.GetFuralityClient();
    if (!fc) return false;

    if (key == "TR") {
        bool now_marked = !fc->IsMarked(ev_->id);
        fc->SetMarked(pda_.GetConfig(), ev_->id, now_marked);
        pda_.GetConfig().Flush();
        if (!now_marked) pda_.ClearFurNotifiedFor(ev_->id);
        // Just flip the heart on row 6.
        display_.BeginPriority();
        RenderMarkIndicator();
        display_.EndPriority();
        return true;
    }

    if (key == "ML" && page_ > 0) {
        page_--;
        pda_.StartRender(this);
        return true;
    }
    if (key == "BL" && page_ < PageCount() - 1) {
        page_++;
        pda_.StartRender(this);
        return true;
    }
    return false;
}

} // namespace YipOS
