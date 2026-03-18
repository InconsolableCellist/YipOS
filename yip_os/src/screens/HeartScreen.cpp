#include "HeartScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Logger.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace YipOS {

using namespace Glyphs;

HeartScreen::HeartScreen(PDAController& pda) : Screen(pda) {
    name = "HEART";
    macro_index = 4;
    update_interval = 1;
    graph_data_.resize(GRAPH_WIDTH, 0);
}

void HeartScreen::Render() {
    RenderFrame("HEARTBEAT");
    RenderInfoLine();
    RenderScaleBar();
    RenderStatusBar();
}

void HeartScreen::RenderDynamic() {
    RenderInfoLine();
    RenderScaleBar();
    RenderClock();
    RenderCursor();
}

void HeartScreen::RenderInfoLine() {
    auto& d = display_;
    char buf[16];

    // Heart glyph — beat animation when data is live
    if (has_data_ && heart_on_) {
        d.WriteGlyph(1, 1, G_HEART);
    } else if (has_data_) {
        d.WriteChar(1, 1, static_cast<int>(' '));
    } else {
        d.WriteChar(1, 1, static_cast<int>('-'));
    }

    if (!has_data_) {
        d.WriteText(4, 1, "---");
        d.WriteText(17, 1, "---");
        d.WriteText(24, 1, "---");
        d.WriteText(32, 1, "---");
        return;
    }

    std::snprintf(buf, sizeof(buf), "%3d", bpm_);
    d.WriteText(4, 1, buf);
    std::snprintf(buf, sizeof(buf), "%3d", bpm_hi_);
    d.WriteText(17, 1, buf);
    std::snprintf(buf, sizeof(buf), "%3d", bpm_lo_);
    d.WriteText(24, 1, buf);
    if (bpm_count_ > 0) {
        std::snprintf(buf, sizeof(buf), "%3d", bpm_sum_ / bpm_count_);
    } else {
        std::snprintf(buf, sizeof(buf), "---");
    }
    d.WriteText(32, 1, buf);
}

void HeartScreen::RenderScaleBar() {
    char buf[8];
    // High value at top of graph (row 2)
    std::snprintf(buf, sizeof(buf), "%3d", scale_hi_);
    display_.WriteText(1, GRAPH_TOP, buf);
    // Low value at bottom of graph (row 6)
    std::snprintf(buf, sizeof(buf), "%3d", scale_lo_);
    display_.WriteText(1, GRAPH_BOTTOM, buf);
}

void HeartScreen::UpdateScale() {
    if (!has_data_) return;

    int peak = 0;
    int valley = 999;
    for (int v : graph_data_) {
        if (v > 0) {
            peak = std::max(peak, v);
            valley = std::min(valley, v);
        }
    }
    if (peak == 0) return;

    prev_scale_hi_ = scale_hi_;
    prev_scale_lo_ = scale_lo_;

    // Pad range by 10% on each side, minimum 20 BPM range
    int range = peak - valley;
    int pad = std::max(range / 10, 5);
    scale_hi_ = peak + pad;
    scale_lo_ = std::max(valley - pad, 0);
    if (scale_hi_ - scale_lo_ < 20) {
        int mid = (scale_hi_ + scale_lo_) / 2;
        scale_lo_ = mid - 10;
        scale_hi_ = mid + 10;
    }
}

void HeartScreen::DrawColumn(int pos) {
    int val = graph_data_[pos];
    int col = GRAPH_LEFT + pos;

    if (val <= 0 || scale_hi_ <= scale_lo_) {
        for (int cell = 0; cell < GRAPH_HEIGHT; cell++) {
            display_.WriteChar(col, GRAPH_BOTTOM - cell, static_cast<int>(' '));
        }
        return;
    }

    float frac = static_cast<float>(val - scale_lo_) / (scale_hi_ - scale_lo_);
    int fill = std::clamp(static_cast<int>(std::round(frac * GRAPH_LEVELS)), 0, GRAPH_LEVELS);

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

void HeartScreen::ClearColumn(int pos) {
    int col = GRAPH_LEFT + pos;
    for (int cell = 0; cell < GRAPH_HEIGHT; cell++) {
        display_.WriteChar(col, GRAPH_BOTTOM - cell, static_cast<int>(' '));
    }
    graph_data_[pos] = 0;
}

void HeartScreen::Update() {
    // Read HR from OSC (thread-safe)
    bool live = pda_.HasHeartRate();
    if (live) {
        bpm_ = pda_.GetHeartRate();
        has_data_ = true;

        // Track stats
        bpm_hi_ = std::max(bpm_hi_, bpm_);
        bpm_lo_ = std::min(bpm_lo_, bpm_);
        bpm_sum_ += bpm_;
        bpm_count_++;
    } else if (has_data_) {
        // Keep last known BPM but stop heart animation
    }

    // Toggle heart beat indicator
    heart_on_ = live ? !heart_on_ : false;

    // Write to graph
    graph_data_[write_pos_] = has_data_ ? bpm_ : 0;
    UpdateScale();

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

bool HeartScreen::OnInput(const std::string& key) {
    return false;
}

} // namespace YipOS
