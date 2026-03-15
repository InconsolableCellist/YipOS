#include "NetScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "net/NetTracker.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace YipOS {

using namespace Glyphs;

NetScreen::NetScreen(PDAController& pda) : Screen(pda) {
    name = "NET";
    macro_index = 3;
    update_interval = 1;
    graph_data_.resize(GRAPH_WIDTH, 0.0);
}

void NetScreen::Render() {
    RenderFrame("NETWORK");
    RenderInfoLine();
    RenderGraphFull();
    RenderStatusBar();
    Logger::Debug("Net screen rendered");
}

void NetScreen::RenderDynamic() {
    RenderInfoLine();
    RenderClock();
    RenderCursor();
    Logger::Debug("Net screen dynamic rendered");
}

void NetScreen::RenderInfoLine() {
    auto& t = pda_.GetNetTracker();
    char buf[8];

    // Interface name (4 chars)
    std::string iface = t.iface.substr(0, 4);
    while (iface.size() < 4) iface += ' ';
    display_.WriteText(1, 1, iface);

    // DL speed
    display_.WriteGlyph(6, 1, G_DOWN);
    display_.WriteText(7, 1, NetTracker::FmtRate(t.current_dl));

    // UL speed
    display_.WriteGlyph(13, 1, G_UP);
    display_.WriteText(14, 1, NetTracker::FmtRate(t.current_ul));

    // DL total
    display_.WriteText(20, 1, "D:");
    display_.WriteText(22, 1, NetTracker::FmtTotal(t.total_dl));

    // Session time
    std::string st = t.SessionElapsed();
    display_.WriteText(COLS - 1 - static_cast<int>(st.size()), 1, st);
}

void NetScreen::UpdateScale() {
    double peak = 0;
    for (double v : graph_data_) peak = std::max(peak, v);
    scale_ = std::max(peak, 1024.0);
}

void NetScreen::DrawColumn(int pos) {
    double val = graph_data_[pos];
    int col = GRAPH_LEFT + pos;
    int fill = (scale_ > 0) ? static_cast<int>(std::round(val / scale_ * GRAPH_LEVELS)) : 0;
    fill = std::min(fill, GRAPH_LEVELS);

    for (int cell = 0; cell < GRAPH_HEIGHT; cell++) {
        int row = GRAPH_BOTTOM - cell;
        int needed = (cell + 1) * 2;
        if (fill >= needed) {
            display_.WriteGlyph(col, row, G_SOLID);
        } else if (fill >= needed - 1) {
            display_.WriteGlyph(col, row, G_LOWER);
        } else {
            display_.WriteChar(col, row, static_cast<int>(' '));
        }
    }
}

void NetScreen::ClearColumn(int pos) {
    int col = GRAPH_LEFT + pos;
    for (int cell = 0; cell < GRAPH_HEIGHT; cell++) {
        display_.WriteChar(col, GRAPH_BOTTOM - cell, static_cast<int>(' '));
    }
}

void NetScreen::RenderGraphFull() {
    UpdateScale();
    for (int i = 0; i < GRAPH_WIDTH; i++) {
        DrawColumn(i);
    }
}

void NetScreen::Update() {
    auto& t = pda_.GetNetTracker();
    graph_data_[write_pos_] = t.current_dl;
    UpdateScale();

    display_.BeginBuffered();
    DrawColumn(write_pos_);
    for (int offset = 1; offset <= 2; offset++) {
        int gap = (write_pos_ + offset) % GRAPH_WIDTH;
        ClearColumn(gap);
    }
    write_pos_ = (write_pos_ + 1) % GRAPH_WIDTH;
    RenderInfoLine();
}

bool NetScreen::OnInput(const std::string& key) {
    return false;
}

} // namespace YipOS
