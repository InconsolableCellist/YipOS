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
    void UpdateScale();
    void DrawColumn(int pos);
    void ClearColumn(int pos);
    void RenderGraphFull();

    static constexpr int GRAPH_LEFT = 1;
    static constexpr int GRAPH_RIGHT = 38;
    static constexpr int GRAPH_TOP = 2;
    static constexpr int GRAPH_BOTTOM = 6;
    static constexpr int GRAPH_WIDTH = GRAPH_RIGHT - GRAPH_LEFT + 1;   // 38
    static constexpr int GRAPH_HEIGHT = GRAPH_BOTTOM - GRAPH_TOP + 1;  // 5
    static constexpr int GRAPH_LEVELS = GRAPH_HEIGHT * 2;              // 10

    std::vector<double> graph_data_;
    int write_pos_ = 0;
    double scale_ = 1024.0;
};

} // namespace YipOS
