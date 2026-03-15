#pragma once

#include "Screen.hpp"
#include <vector>

namespace YipOS {

class HeartScreen : public Screen {
public:
    HeartScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;
    void Update() override;

private:
    void RenderInfoLine();
    void DrawColumn(int pos);
    void ClearColumn(int pos);
    int BpmToLevel(int bpm) const;
    void AdvanceBpm();

    static constexpr int BPM_MIN = 60;
    static constexpr int BPM_MAX = 130;
    static constexpr int GRAPH_LEFT = 5;
    static constexpr int GRAPH_RIGHT = 38;
    static constexpr int GRAPH_TOP = 2;
    static constexpr int GRAPH_BOTTOM = 6;
    static constexpr int GRAPH_WIDTH = GRAPH_RIGHT - GRAPH_LEFT + 1;   // 34
    static constexpr int GRAPH_HEIGHT = GRAPH_BOTTOM - GRAPH_TOP + 1;  // 5
    static constexpr int GRAPH_LEVELS = GRAPH_HEIGHT * 2;              // 10

    int bpm_ = 72;
    int bpm_hi_ = 72;
    int bpm_lo_ = 72;
    int bpm_sum_ = 72;
    int bpm_count_ = 1;
    bool heart_on_ = true;

    std::vector<int> graph_data_;
    int write_pos_ = 0;
};

} // namespace YipOS
