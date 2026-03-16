#include "VRCXScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Logger.hpp"
#include <cstring>

namespace YipOS {

using namespace Glyphs;

VRCXScreen::VRCXScreen(PDAController& pda) : Screen(pda) {
    name = "VRCX";
    macro_index = 8;
    for (auto& row : tile_highlighted_) row.fill(false);
}

void VRCXScreen::Render() {
    RenderFrame("VRCX");

    for (int ty = 0; ty < 2; ty++) {
        int row = ZONE_ROWS[ty];
        display_.WriteGlyph(0, row, G_VLINE);
        for (int tx = 0; tx < 3; tx++) {
            WriteTile(tx, ty);
        }
        display_.WriteGlyph(COLS - 1, row, G_VLINE);
    }

    RenderStatusBar();
}

void VRCXScreen::RenderDynamic() {
    RenderClock();
    RenderCursor();
}

void VRCXScreen::WriteTileLine(int tx, int ty, const char* text, int row) {
    auto& tile = TILES[ty][tx];
    bool is_active = tile.screen_name != nullptr;
    bool inverted = is_active && !tile_highlighted_[ty][tx];

    int len = static_cast<int>(std::strlen(text));
    int center = BTN_COLS[tx];
    int start_col = center - len / 2;

    for (int i = 0; i < len; i++) {
        int c = start_col + i;
        if (c < 1 || c >= COLS - 1) continue;
        int char_idx = static_cast<int>(text[i]);
        if (inverted) char_idx += INVERT_OFFSET;
        display_.WriteChar(c, row, char_idx);
    }
}

void VRCXScreen::WriteTile(int tx, int ty) {
    auto& tile = TILES[ty][tx];
    int row = ZONE_ROWS[ty];
    WriteTileLine(tx, ty, tile.line1, row);
    if (tile.line2) {
        WriteTileLine(tx, ty, tile.line2, row + 1);
    }
}

bool VRCXScreen::OnInput(const std::string& key) {
    if (key.size() != 2) return false;
    int tx = key[0] - '1';
    int ty = key[1] - '1';

    // Map touch cols 1,3,5 to tile indices 0,1,2
    int tile_x = -1;
    if (tx == 0) tile_x = 0;
    else if (tx == 2) tile_x = 1;
    else if (tx == 4) tile_x = 2;

    if (tile_x < 0 || ty < 0 || ty > 1) return false;

    auto& tile = TILES[ty][tile_x];
    if (!tile.screen_name) {
        // Inactive tile
        return true;
    }

    // Highlight (un-invert) and navigate
    display_.CancelBuffered();
    tile_highlighted_[ty][tile_x] = true;
    WriteTile(tile_x, ty);

    Logger::Info("VRCX -> " + std::string(tile.screen_name));
    pda_.SetPendingNavigate(tile.screen_name);
    return true;
}

} // namespace YipOS
