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
    void RenderSinglePart(int part_row, int part_col, bool flash = false);
    void SendLock(int spvr_index, bool lock);
    void SendGlobalLock(bool lock);
    const char* StatusLabel(int status) const;
    int GetStatus(int part_row, int part_col) const;
    bool AnyLocked() const;

    static constexpr int PART_ROWS = 2;
    static constexpr int PART_ACTIVE_COLS = 3;

    static const char* DISPLAY_LABELS[PART_ROWS][PART_ACTIVE_COLS];
    static const int SPVR_INDEX[PART_ROWS][PART_ACTIVE_COLS];
    static const std::array<int, 3> TILE_POSITIONS;

    struct PartMapping { int row; int col; };
    static const std::unordered_map<std::string, PartMapping> CONTACT_MAP;

    int last_rendered_[PART_ROWS][PART_ACTIVE_COLS] = {};
};

} // namespace YipOS
