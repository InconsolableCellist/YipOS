#pragma once

#include "Screen.hpp"
#include <vector>

namespace YipOS {

class NetScreen : public Screen {
public:
    NetScreen(PDAController& pda);

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
    void RenderGraphFull();

    static std::string ScaleFmt(double val);

    static constexpr int GRAPH_LEFT = 4;
    static constexpr int GRAPH_RIGHT = 38;
    static constexpr int GRAPH_TOP = 2;
    static constexpr int GRAPH_BOTTOM = 6;
    static constexpr int GRAPH_WIDTH = GRAPH_RIGHT - GRAPH_LEFT + 1;   // 35
    static constexpr int GRAPH_HEIGHT = GRAPH_BOTTOM - GRAPH_TOP + 1;  // 5
    static constexpr int GRAPH_LEVELS = GRAPH_HEIGHT * 2;              // 10

    static constexpr const char* SCALE_LABELS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static constexpr int MAX_LABEL = 35;

    std::vector<double> graph_data_;
    int write_pos_ = 0;
    double scale_ = 1024.0;
    double prev_scale_ = 1024.0;
    int scale_label_idx_ = 0;
    std::vector<char> column_labels_;  // label char per graph column (0 = none)
    bool clearing_ = false;
};

} // namespace YipOS
