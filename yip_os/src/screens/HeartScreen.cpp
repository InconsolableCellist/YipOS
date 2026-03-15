#include "HeartScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Logger.hpp"
#include <cstdio>
#include <cmath>
#include <random>
#include <algorithm>

namespace YipOS {

using namespace Glyphs;

// Thread-local RNG
static std::mt19937& GetRNG() {
    static thread_local std::mt19937 rng(std::random_device{}());
    return rng;
}

HeartScreen::HeartScreen(PDAController& pda) : Screen(pda) {
    name = "HEART";
    macro_index = 4;
    update_interval = 1;
    graph_data_.resize(GRAPH_WIDTH, 0);
}

void HeartScreen::AdvanceBpm() {
    static constexpr int deltas[] = {-3, -2, -1, -1, 0, 0, 0, 1, 1, 2, 3};
    std::uniform_int_distribution<int> dist(0, 10);
    int delta = deltas[dist(GetRNG())];
    bpm_ = std::clamp(bpm_ + delta, 55, 120);
    bpm_hi_ = std::max(bpm_hi_, bpm_);
    bpm_lo_ = std::min(bpm_lo_, bpm_);
    bpm_sum_ += bpm_;
    bpm_count_++;
}

int HeartScreen::BpmToLevel(int bpm) const {
    float frac = static_cast<float>(bpm - BPM_MIN) / (BPM_MAX - BPM_MIN);
    return std::clamp(static_cast<int>(std::round(frac * GRAPH_LEVELS)), 0, GRAPH_LEVELS);
}

void HeartScreen::Render() {
    RenderFrame("HEARTBEAT");
    RenderInfoLine();
    RenderStatusBar();
    Logger::Debug("Heart screen rendered");
}

void HeartScreen::RenderDynamic() {
    RenderInfoLine();
    RenderClock();
    RenderCursor();
    Logger::Debug("Heart screen dynamic rendered");
}

void HeartScreen::RenderInfoLine() {
    char buf[16];
    if (heart_on_) {
        display_.WriteGlyph(1, 1, G_HEART);
    } else {
        display_.WriteChar(1, 1, static_cast<int>(' '));
    }
    std::snprintf(buf, sizeof(buf), "%3d", bpm_);
    display_.WriteText(4, 1, buf);
    std::snprintf(buf, sizeof(buf), "%3d", bpm_hi_);
    display_.WriteText(17, 1, buf);
    std::snprintf(buf, sizeof(buf), "%3d", bpm_lo_);
    display_.WriteText(24, 1, buf);
    std::snprintf(buf, sizeof(buf), "%3d", bpm_sum_ / bpm_count_);
    display_.WriteText(32, 1, buf);
}

void HeartScreen::DrawColumn(int pos) {
    int val = graph_data_[pos];
    int col = GRAPH_LEFT + pos;
    int fill = (val > 0) ? BpmToLevel(val) : 0;

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
}

void HeartScreen::Update() {
    AdvanceBpm();
    heart_on_ = !heart_on_;

    graph_data_[write_pos_] = bpm_;

    display_.BeginBuffered();
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
