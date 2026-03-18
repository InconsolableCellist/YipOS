#pragma once

#include "Screen.hpp"
#include <vector>

namespace YipOS {

class BFIScreen : public Screen {
public:
    BFIScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;
    void Update() override;

private:
    void RenderInfoLine();
    void RenderScaleBar();
    void UpdateScale();
    void DrawColumn(int pos);
    void ClearColumn(int pos);

    static constexpr int GRAPH_LEFT = 5;
    static constexpr int GRAPH_RIGHT = 38;
    static constexpr int GRAPH_TOP = 1;
    static constexpr int GRAPH_BOTTOM = 5;
    static constexpr int GRAPH_WIDTH = GRAPH_RIGHT - GRAPH_LEFT + 1;   // 34
    static constexpr int GRAPH_HEIGHT = GRAPH_BOTTOM - GRAPH_TOP + 1;  // 5
    static constexpr int GRAPH_LEVELS = GRAPH_HEIGHT * 2;              // 10

    // Which BFI param to display (index into BFI_PARAM_NAMES)
    int param_idx_ = 2;  // FocusAvg
    int prev_param_idx_ = -1;  // track changes for info line refresh

    // Scale (fixed, derived from selected param)
    float scale_hi_ = 1.0f;
    float scale_lo_ = -1.0f;

    // Circular buffer for trace
    std::vector<float> trace_;
    std::vector<int> dot_rows_;  // cached dot row per column (-1 = none)
    int write_pos_ = 0;
    bool has_data_ = false;
};

} // namespace YipOS
