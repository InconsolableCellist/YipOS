#include "FurDetailScreen.hpp"

#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Config.hpp"
#include "core/Glyphs.hpp"
#include "net/FuralityClient.hpp"

#include <cstdio>
#include <ctime>
#include <string>

namespace YipOS {

using namespace Glyphs;

FurDetailScreen::FurDetailScreen(PDAController& pda) : Screen(pda) {
    name = "FUR_DTL";
    macro_index = -1;
    refresh_interval = -1;
    ev_ = pda_.GetSelectedFurEvent();
}

void FurDetailScreen::Render() {
    RenderFrame("EVENT");
    RenderContent();
    RenderMarkRow();
    RenderStatusBar();
}

void FurDetailScreen::RenderDynamic() {
    RenderClock();
    RenderCursor();
}

std::string FurDetailScreen::FormatTimeRange() const {
    if (!ev_) return {};
    char buf[32];
    if (ev_->start_unix <= 0) return {};
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

void FurDetailScreen::RenderContent() {
    auto& d = display_;
    int max_w = COLS - 2;  // 38

    if (!ev_) {
        d.WriteText(2, 3, "No event selected");
        return;
    }

    // Row 1: title (truncated)
    std::string title = ev_->title.empty() ? std::string("(untitled)") : ev_->title;
    if (static_cast<int>(title.size()) > max_w) title = title.substr(0, max_w);
    d.WriteText(1, 1, title);

    // Row 2: time range
    std::string when = FormatTimeRange();
    if (!when.empty()) {
        if (static_cast<int>(when.size()) > max_w) when = when.substr(0, max_w);
        d.WriteText(1, 2, when);
    }

    // Row 3: host / location
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

    // Rows 4-6: description, word-wrapped
    std::string desc = ev_->description;
    for (auto& c : desc) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        else if (static_cast<unsigned char>(c) < 32) c = ' ';
    }
    int row = 4;
    int col = 0;
    for (size_t i = 0; i < desc.size() && row <= 6; i++) {
        if (col >= max_w) {
            row++;
            col = 0;
            if (row > 6) break;
        }
        char c = desc[i];
        if (c < 32 || static_cast<unsigned char>(c) > 126) c = '?';
        d.WriteChar(1 + col, row, static_cast<int>(c));
        col++;
    }
}

void FurDetailScreen::RenderMarkRow() {
    auto& d = display_;
    auto* fc = pda_.GetFuralityClient();
    bool marked = (ev_ && fc && fc->IsMarked(ev_->id));

    // Render the mark indicator on row 6 right-side (so it doesn't fight the
    // description). Inverted block when marked.
    const char* tag = marked ? " MARKED  TR=UNMARK " : "  TR = MARK INTEREST";
    int len = static_cast<int>(std::char_traits<char>::length(tag));
    int col = COLS - 1 - len;
    if (col < 1) col = 1;

    for (int i = 0; i < len; i++) {
        int ch = static_cast<int>(tag[i]);
        if (marked) ch += INVERT_OFFSET;
        d.WriteChar(col + i, 6, ch);
    }

    if (marked) d.WriteGlyph(col - 1 < 1 ? 1 : col - 1, 6, G_HEART);
}

bool FurDetailScreen::OnInput(const std::string& key) {
    if (!ev_) return false;
    auto* fc = pda_.GetFuralityClient();
    if (!fc) return false;

    if (key == "TR") {
        bool now_marked = !fc->IsMarked(ev_->id);
        fc->SetMarked(pda_.GetConfig(), ev_->id, now_marked);
        pda_.GetConfig().Flush();
        // If we just unmarked, also clear any pending notification flag
        if (!now_marked) pda_.ClearFurNotifiedFor(ev_->id);
        // Re-render the mark row
        display_.BeginPriority();
        RenderMarkRow();
        display_.EndPriority();
        return true;
    }
    return false;
}

} // namespace YipOS
