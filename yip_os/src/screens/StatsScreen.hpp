#pragma once

#include "Screen.hpp"
#include <string>
#include <map>

namespace YipOS {

class StatsScreen : public Screen {
public:
    StatsScreen(PDAController& pda);

    void Render() override;
    void RenderContent() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;
    void Update() override;

private:
    struct Stats {
        int cpu_pct = 0;
        int cpu_temp = 0;
        int mem_pct = 0;
        std::string mem_text;
        int gpu_pct = 0;
        int gpu_temp = 0;
        float net_up = 0;
        float net_down = 0;
        int disk_pct = 0;
        std::string disk_label;
        std::string uptime;
    };

    Stats GetStats();
    void RenderStats(const Stats& s);
    void Bar(int col, int row, int width, float frac);
    void BarFilledOnly(int col, int row, int width, float frac);
    void BarDelta(int col, int row, int width, float new_frac, float old_frac);
    static std::string NetFmt(float mbps);

    static constexpr int BAR_COL = 10;
    static constexpr int BAR_WIDTH = 20;

    Stats last_stats_{};
    bool has_last_ = false;
};

} // namespace YipOS
