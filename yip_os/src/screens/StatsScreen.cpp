#include "StatsScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "net/NetTracker.hpp"
#include "platform/SystemStats.hpp"
#include "core/Logger.hpp"
#include <cstdio>
#include <algorithm>

namespace YipOS {

using namespace Glyphs;

StatsScreen::StatsScreen(PDAController& pda) : Screen(pda) {
    name = "STATS";
    macro_index = 1;
    refresh_interval = 0;
    update_interval = 2;
}

StatsScreen::Stats StatsScreen::GetStats() {
    auto& sys = pda_.GetSystemStats();
    Stats s;
    s.cpu_pct = sys.GetCPUPercent();
    s.cpu_temp = sys.GetCPUTemp();
    s.mem_pct = sys.GetMemPercent();
    s.mem_text = sys.GetMemText();
    s.gpu_pct = sys.GetGPUPercent();
    s.gpu_temp = sys.GetGPUTemp();
    s.net_up = 0;
    s.net_down = 0;
    s.disk_pct = sys.GetDiskPercent();
    s.disk_label = sys.GetDiskLabel();
    s.uptime = sys.GetUptime();
    return s;
}

std::string StatsScreen::NetFmt(float mbps) {
    char buf[8];
    if (mbps < 1.0f) {
        std::snprintf(buf, sizeof(buf), "%4.0fk", mbps * 1000);
    } else {
        std::snprintf(buf, sizeof(buf), "%4.1fM", mbps);
    }
    return buf;
}

void StatsScreen::Render() {
    RenderFrame("SYSTEM STATUS");
    Stats s = GetStats();
    RenderStats(s);
    last_stats_ = s;
    has_last_ = true;
    RenderStatusBar();
    Logger::Debug("Stats screen rendered");
}

void StatsScreen::RenderContent() {
    Stats s = GetStats();
    RenderStats(s);
    last_stats_ = s;
    has_last_ = true;
}

void StatsScreen::RenderStats(const Stats& s) {
    auto& d = display_;
    char buf[16];

    // Row 1: CPU
    d.WriteText(1, 1, "CPU");
    std::snprintf(buf, sizeof(buf), "%3d%%", s.cpu_pct);
    d.WriteText(5, 1, buf);
    Bar(BAR_COL, 1, BAR_WIDTH, s.cpu_pct / 100.0f);
    std::snprintf(buf, sizeof(buf), "%2d", s.cpu_temp);
    d.WriteText(31, 1, buf);
    d.WriteGlyph(33, 1, G_GEAR);
    d.WriteText(34, 1, "C");

    // Row 2: MEM
    d.WriteText(1, 2, "MEM");
    std::snprintf(buf, sizeof(buf), "%3d%%", s.mem_pct);
    d.WriteText(5, 2, buf);
    Bar(BAR_COL, 2, BAR_WIDTH, s.mem_pct / 100.0f);
    d.WriteText(31, 2, s.mem_text);

    // Row 3: GPU
    d.WriteText(1, 3, "GPU");
    std::snprintf(buf, sizeof(buf), "%3d%%", s.gpu_pct);
    d.WriteText(5, 3, buf);
    Bar(BAR_COL, 3, BAR_WIDTH, s.gpu_pct / 100.0f);
    std::snprintf(buf, sizeof(buf), "%2d", s.gpu_temp);
    d.WriteText(31, 3, buf);
    d.WriteGlyph(33, 3, G_GEAR);
    d.WriteText(34, 3, "C");

    // Row 4: NET
    d.WriteText(1, 4, "NET");
    d.WriteGlyph(5, 4, G_UP);
    d.WriteText(6, 4, NetFmt(s.net_up));
    d.WriteGlyph(11, 4, G_DOWN);
    d.WriteText(12, 4, NetFmt(s.net_down));

    // Row 5: DISK
    d.WriteText(1, 5, "DISK");
    std::snprintf(buf, sizeof(buf), "%3d%%", s.disk_pct);
    d.WriteText(5, 5, buf);
    Bar(BAR_COL, 5, BAR_WIDTH, s.disk_pct / 100.0f);
    std::snprintf(buf, sizeof(buf), "%4s", s.disk_label.c_str());
    d.WriteText(31, 5, buf);

    // Row 6: UPTIME
    d.WriteText(1, 6, "UP");
    d.WriteText(5, 6, s.uptime);
    d.WriteText(24, 6, "YIP");
    d.WriteText(28, 6, pda_.GetNetTracker().SessionElapsed());
}

void StatsScreen::RenderDynamic() {
    Stats s = GetStats();
    auto& d = display_;
    char buf[16];

    std::snprintf(buf, sizeof(buf), "%3d%%", s.cpu_pct);
    d.WriteText(5, 1, buf);
    BarFilledOnly(BAR_COL, 1, BAR_WIDTH, s.cpu_pct / 100.0f);
    std::snprintf(buf, sizeof(buf), "%2d", s.cpu_temp);
    d.WriteText(31, 1, buf);

    std::snprintf(buf, sizeof(buf), "%3d%%", s.mem_pct);
    d.WriteText(5, 2, buf);
    BarFilledOnly(BAR_COL, 2, BAR_WIDTH, s.mem_pct / 100.0f);
    d.WriteText(31, 2, s.mem_text);

    std::snprintf(buf, sizeof(buf), "%3d%%", s.gpu_pct);
    d.WriteText(5, 3, buf);
    BarFilledOnly(BAR_COL, 3, BAR_WIDTH, s.gpu_pct / 100.0f);
    std::snprintf(buf, sizeof(buf), "%2d", s.gpu_temp);
    d.WriteText(31, 3, buf);

    d.WriteText(6, 4, NetFmt(s.net_up));
    d.WriteText(12, 4, NetFmt(s.net_down));

    std::snprintf(buf, sizeof(buf), "%3d%%", s.disk_pct);
    d.WriteText(5, 5, buf);
    BarFilledOnly(BAR_COL, 5, BAR_WIDTH, s.disk_pct / 100.0f);
    std::snprintf(buf, sizeof(buf), "%4s", s.disk_label.c_str());
    d.WriteText(31, 5, buf);

    d.WriteText(5, 6, s.uptime);
    d.WriteText(28, 6, pda_.GetNetTracker().SessionElapsed());

    last_stats_ = s;
    has_last_ = true;
    RenderClock();
    RenderCursor();
    Logger::Debug("Stats screen dynamic rendered");
}

void StatsScreen::Update() {
    Stats s = GetStats();
    if (!has_last_) return;
    auto& d = display_;
    char buf[16];

    // Use buffered writes so the main loop drains them one at a time,
    // keeping the UI responsive instead of blocking 70ms * N writes.
    d.BeginBuffered();

    if (s.cpu_pct != last_stats_.cpu_pct) {
        std::snprintf(buf, sizeof(buf), "%3d%%", s.cpu_pct);
        d.WriteText(5, 1, buf);
        BarDelta(BAR_COL, 1, BAR_WIDTH, s.cpu_pct / 100.0f, last_stats_.cpu_pct / 100.0f);
    }
    if (s.cpu_temp != last_stats_.cpu_temp) {
        std::snprintf(buf, sizeof(buf), "%2d", s.cpu_temp);
        d.WriteText(31, 1, buf);
    }
    if (s.mem_pct != last_stats_.mem_pct) {
        std::snprintf(buf, sizeof(buf), "%3d%%", s.mem_pct);
        d.WriteText(5, 2, buf);
        BarDelta(BAR_COL, 2, BAR_WIDTH, s.mem_pct / 100.0f, last_stats_.mem_pct / 100.0f);
    }
    if (s.gpu_pct != last_stats_.gpu_pct) {
        std::snprintf(buf, sizeof(buf), "%3d%%", s.gpu_pct);
        d.WriteText(5, 3, buf);
        BarDelta(BAR_COL, 3, BAR_WIDTH, s.gpu_pct / 100.0f, last_stats_.gpu_pct / 100.0f);
    }
    if (s.gpu_temp != last_stats_.gpu_temp) {
        std::snprintf(buf, sizeof(buf), "%2d", s.gpu_temp);
        d.WriteText(31, 3, buf);
    }
    if (s.disk_pct != last_stats_.disk_pct) {
        std::snprintf(buf, sizeof(buf), "%3d%%", s.disk_pct);
        d.WriteText(5, 5, buf);
        BarDelta(BAR_COL, 5, BAR_WIDTH, s.disk_pct / 100.0f, last_stats_.disk_pct / 100.0f);
    }

    d.WriteText(5, 6, s.uptime);
    d.WriteText(28, 6, pda_.GetNetTracker().SessionElapsed());
    last_stats_ = s;
}

bool StatsScreen::OnInput(const std::string& key) {
    if (key == "52") {
        // Cycle filesystem — "DSK" button at col 36, row 4 (touch 52)
        auto& sys = pda_.GetSystemStats();

        // Flash: toggle to normal text (un-inverted)
        display_.WriteText(35, 4, "DSK", false);

        sys.CycleDisk();
        Logger::Info("Disk cycled to: " + sys.GetDiskLabel());

        // Redraw the DISK row with new filesystem data
        Stats s = GetStats();
        auto& d = display_;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%3d%%", s.disk_pct);
        d.WriteText(5, 5, buf);
        Bar(BAR_COL, 5, BAR_WIDTH, s.disk_pct / 100.0f);
        std::snprintf(buf, sizeof(buf), "%4s", s.disk_label.c_str());
        d.WriteText(31, 5, buf);

        // Restore: back to inverted
        d.WriteText(35, 4, "DSK", true);

        last_stats_.disk_pct = s.disk_pct;
        last_stats_.disk_label = s.disk_label;
        return true;
    }
    return false;
}

void StatsScreen::Bar(int col, int row, int width, float frac) {
    int filled = static_cast<int>(frac * width);
    for (int c = 0; c < width; c++) {
        display_.WriteGlyph(col + c, row, c < filled ? G_SOLID : G_SHADE1);
    }
}

void StatsScreen::BarFilledOnly(int col, int row, int width, float frac) {
    int filled = static_cast<int>(frac * width);
    for (int c = 0; c < filled; c++) {
        display_.WriteGlyph(col + c, row, G_SOLID);
    }
}

void StatsScreen::BarDelta(int col, int row, int width, float new_frac, float old_frac) {
    int new_filled = static_cast<int>(new_frac * width);
    int old_filled = static_cast<int>(old_frac * width);
    if (new_filled == old_filled) return;

    // Only write the cells that changed between old and new fill levels
    int lo = std::min(old_filled, new_filled);
    int hi = std::max(old_filled, new_filled);
    for (int c = lo; c < hi; c++) {
        display_.WriteGlyph(col + c, row, c < new_filled ? G_SOLID : G_SHADE1);
    }
}

} // namespace YipOS
