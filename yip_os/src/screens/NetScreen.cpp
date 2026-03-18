#include "NetScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "net/NetTracker.hpp"
#include "core/Config.hpp"
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
    column_labels_.resize(GRAPH_WIDTH, 0);
}

void NetScreen::Render() {
    RenderFrame("NETWORK");
    RenderInfoLine();
    RenderScaleBar();
    RenderGraphFull();
    RenderStatusBar();
    Logger::Debug("Net screen rendered");
}

void NetScreen::RenderDynamic() {
    RenderInfoLine();
    RenderScaleBar();
    RenderClock();
    RenderCursor();
    Logger::Debug("Net screen dynamic rendered");
}

void NetScreen::RenderInfoLine() {
    auto& t = pda_.GetNetTracker();

    // Interface name (4 chars) — inverted = touchable button
    std::string iface = t.iface.substr(0, 4);
    while (iface.size() < 4) iface += ' ';
    display_.WriteText(1, 1, iface, true);

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

std::string NetScreen::ScaleFmt(double val) {
    char buf[8];
    if (val < 1000) {
        std::snprintf(buf, sizeof(buf), "%3.0f", val);
    } else {
        val /= 1024;
        if (val < 99.5) {
            std::snprintf(buf, sizeof(buf), "%2.0fk", val);
        } else {
            val /= 1024;
            if (val < 99.5) {
                std::snprintf(buf, sizeof(buf), "%2.0fM", val);
            } else {
                val /= 1024;
                std::snprintf(buf, sizeof(buf), "%2.0fG", val);
            }
        }
    }
    return buf;
}

void NetScreen::RenderScaleBar() {
    // High value at top of graph area (row 2), 3 chars in cols 1-3
    std::string hi = ScaleFmt(scale_);
    display_.WriteText(1, GRAPH_TOP, hi);

    // Current scale label at bottom of graph area (row 6)
    char label = SCALE_LABELS[std::min(scale_label_idx_, MAX_LABEL)];
    display_.WriteChar(1, GRAPH_BOTTOM, static_cast<int>(label));
    display_.WriteChar(2, GRAPH_BOTTOM, static_cast<int>(' '));
    display_.WriteChar(3, GRAPH_BOTTOM, static_cast<int>(' '));
}

void NetScreen::UpdateScale() {
    double peak = 0;
    for (double v : graph_data_) peak = std::max(peak, v);
    prev_scale_ = scale_;
    scale_ = std::max(peak, 1024.0);
}

void NetScreen::DrawColumn(int pos) {
    double val = graph_data_[pos];
    int col = GRAPH_LEFT + pos;
    int fill = (scale_ > 0) ? static_cast<int>(std::round(val / scale_ * GRAPH_LEVELS)) : 0;
    fill = std::min(fill, GRAPH_LEVELS);

    for (int cell = 0; cell < GRAPH_HEIGHT; cell++) {
        int row = GRAPH_BOTTOM - cell;

        // Bottom cell: show scale change label if present
        if (cell == 0 && column_labels_[pos] != 0) {
            display_.WriteChar(col, row, static_cast<int>(column_labels_[pos]));
            continue;
        }

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
    graph_data_[pos] = 0.0;
    column_labels_[pos] = 0;
}

void NetScreen::RenderGraphFull() {
    UpdateScale();
    for (int i = 0; i < GRAPH_WIDTH; i++) {
        DrawColumn(i);
    }
}

void NetScreen::Update() {
    auto& t = pda_.GetNetTracker();

    // Clear old label at write position before overwriting
    column_labels_[write_pos_] = 0;
    graph_data_[write_pos_] = t.current_dl;
    UpdateScale();

    // Detect scale change — mark the triggering column
    if (scale_ != prev_scale_) {
        if (scale_ < prev_scale_) {
            // Scale decreased (old peaks cleared) — reset label sequence
            scale_label_idx_ = 0;
        } else {
            scale_label_idx_ = std::min(scale_label_idx_ + 1, MAX_LABEL);
        }
        char label = SCALE_LABELS[scale_label_idx_];
        column_labels_[write_pos_] = label;
    }

    display_.BeginBuffered();
    RenderScaleBar();
    DrawColumn(write_pos_);
    for (int offset = 1; offset <= 2; offset++) {
        int gap = (write_pos_ + offset) % GRAPH_WIDTH;
        ClearColumn(gap);
    }
    write_pos_ = (write_pos_ + 1) % GRAPH_WIDTH;
    RenderInfoLine();
}

bool NetScreen::OnInput(const std::string& key) {
    if (key == "11") {
        auto& t = pda_.GetNetTracker();

        // Flash: un-invert the interface name
        std::string old_name = t.iface.substr(0, 4);
        while (old_name.size() < 4) old_name += ' ';
        display_.WriteText(1, 1, old_name, false);

        t.CycleInterface();
        pda_.GetConfig().SetState("net.interface", t.iface);

        // Reset graph state for new interface
        std::fill(graph_data_.begin(), graph_data_.end(), 0.0);
        std::fill(column_labels_.begin(), column_labels_.end(), 0);
        write_pos_ = 0;
        scale_ = 1024.0;
        prev_scale_ = 1024.0;
        scale_label_idx_ = 0;

        // Full re-render via macro stamp
        pda_.StartRender(this);
        Logger::Info("Net interface cycled to: " + t.iface);
        return true;
    }
    return false;
}

} // namespace YipOS
