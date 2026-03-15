#pragma once

#include "Screen.hpp"
#include <string>
#include <unordered_map>
#include <array>

namespace YipOS {

class StayScreen : public Screen {
public:
    StayScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;
    void Update() override;

private:
    void RenderPartRow(int part_row, int display_row);
    void RenderSinglePart(const std::string& part, int part_row, int part_col, bool flash = false);

    // Body part names [row][col] — nullptr for unused slots
    static constexpr int PART_ROWS = 2;
    static constexpr int PART_ACTIVE_COLS = 3;
    static const char* BODY_PARTS[PART_ROWS][5];
    static const std::array<int, 3> TILE_POSITIONS;

    // Contact map: input key -> (part_row, part_col)
    struct PartMapping { int row; int col; };
    static const std::unordered_map<std::string, PartMapping> CONTACT_MAP;

    std::unordered_map<std::string, bool> locked_;
    bool connected_ = true;
    float drift_ = 0.02f;
};

} // namespace YipOS
