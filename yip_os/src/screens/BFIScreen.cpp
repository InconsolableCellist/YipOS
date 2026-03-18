#include "BFIScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Config.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <string>

namespace YipOS {

using namespace Glyphs;

static bool IsPosPar(int idx) {
    if (idx >= 0 && idx < PDAController::BFI_PARAM_COUNT)
        return PDAController::BFI_PARAMS[idx].positive_only;
    return false;
}

BFIScreen::BFIScreen(PDAController& pda) : Screen(pda) {
    name = "BFI";
    macro_index = 22;
    update_interval = 0.5f;
    refresh_interval = -1;  // disable periodic macro re-stamp; Update() handles all drawing
    trace_.resize(GRAPH_WIDTH, 0);
    dot_rows_.resize(GRAPH_WIDTH, -1);

    std::string p = pda.GetConfig().GetState("bfi.param", "2");
    param_idx_ = std::clamp(std::stoi(p), 0, PDAController::BFI_PARAM_COUNT - 1);
    UpdateScale();
}

void BFIScreen::UpdateScale() {
    if (IsPosPar(param_idx_)) {
        scale_lo_ = 0.0f;
        scale_hi_ = 1.0f;
    } else {
        scale_lo_ = -1.0f;
        scale_hi_ = 1.0f;
    }
}

void BFIScreen::Render() {
    RenderFrame("BFIVRC");
    RenderScaleBar();
    RenderInfoLine();
    RenderStatusBar();
}

void BFIScreen::RenderDynamic() {
    // Re-read param selection in case it changed via param picker
    std::string p = pda_.GetConfig().GetState("bfi.param", "2");
    param_idx_ = std::clamp(std::stoi(p), 0, PDAController::BFI_PARAM_COUNT - 1);
    UpdateScale();

    RenderScaleBar();
    // Only rewrite info line if param changed (avoids overwriting CONF button needlessly)
    if (param_idx_ != prev_param_idx_) {
        RenderInfoLine();
        prev_param_idx_ = param_idx_;
    }
    RenderClock();
    RenderCursor();
}

void BFIScreen::RenderScaleBar() {
    if (scale_lo_ < 0) {
        display_.WriteText(1, GRAPH_TOP, " 1.0");
        display_.WriteText(1, (GRAPH_TOP + GRAPH_BOTTOM) / 2, " 0.0");
        display_.WriteText(1, GRAPH_BOTTOM, "-1.0");
    } else {
        display_.WriteText(1, GRAPH_TOP, "1.0 ");
        display_.WriteText(1, (GRAPH_TOP + GRAPH_BOTTOM) / 2, "0.5 ");
        display_.WriteText(1, GRAPH_BOTTOM, "0.0 ");
    }
}

void BFIScreen::RenderInfoLine() {
    auto& d = display_;
    const char* pname = PDAController::BFI_PARAMS[param_idx_].display_name;

    // Clear row 6 content area BUT stop before CONF button (cols 35-38)
    for (int c = 1; c < 34; c++)
        d.WriteChar(c, 6, ' ');

    d.WriteText(1, 6, pname);
    prev_param_idx_ = param_idx_;
}

void BFIScreen::DrawColumn(int pos) {
    int col = GRAPH_LEFT + pos;
    float val = trace_[pos];

    // Map value to a row
    int new_row = -1;
    int dot_glyph = 0;
    if (has_data_) {
        float frac = (val - scale_lo_) / (scale_hi_ - scale_lo_);
        int level = std::clamp(static_cast<int>(std::round(frac * GRAPH_LEVELS)), 0, GRAPH_LEVELS);
        if (level > 0) {
            int cell = (level - 1) / 2;
            int half = (level - 1) % 2;
            new_row = GRAPH_BOTTOM - cell;
            dot_glyph = (half == 1) ? G_UPPER : G_LOWER;
        }
    }

    // Clear old dot, draw new dot (max 2 writes)
    int old_row = dot_rows_[pos];
    if (old_row >= 0 && old_row != new_row)
        display_.WriteChar(col, old_row, ' ');
    if (new_row >= 0)
        display_.WriteGlyph(col, new_row, dot_glyph);
    dot_rows_[pos] = new_row;
}

void BFIScreen::ClearColumn(int pos) {
    int col = GRAPH_LEFT + pos;
    // Full clear — wipes any leftover from previous sweep pass
    for (int cell = 0; cell < GRAPH_HEIGHT; cell++)
        display_.WriteChar(col, GRAPH_BOTTOM - cell, ' ');
    dot_rows_[pos] = -1;
    trace_[pos] = 0;
}

void BFIScreen::Update() {
    // Re-read param selection from config (may have changed via param picker)
    std::string p = pda_.GetConfig().GetState("bfi.param", "2");
    int new_idx = std::clamp(std::stoi(p), 0, PDAController::BFI_PARAM_COUNT - 1);
    bool param_changed = (new_idx != param_idx_);
    if (param_changed) {
        param_idx_ = new_idx;
        UpdateScale();
    }

    bool live = pda_.HasBFIData();
    if (live) {
        has_data_ = true;
        trace_[write_pos_] = pda_.GetBFIParam(param_idx_);
    } else {
        trace_[write_pos_] = 0;
    }

    display_.BeginBuffered();
    if (param_changed) RenderScaleBar();
    DrawColumn(write_pos_);
    // Clear one column ahead as the sweep gap
    int gap = (write_pos_ + 1) % GRAPH_WIDTH;
    ClearColumn(gap);
    write_pos_ = (write_pos_ + 1) % GRAPH_WIDTH;
    if (param_changed) RenderInfoLine();
}

bool BFIScreen::OnInput(const std::string& key) {
    // Touch 53 → go straight to param picker
    if (key == "53") {
        pda_.SetPendingNavigate("BFI_PARAM");
        return true;
    }
    return false;
}

} // namespace YipOS
