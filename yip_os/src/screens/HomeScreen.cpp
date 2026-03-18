#include "HomeScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Logger.hpp"
#include <cstring>

namespace YipOS {

using namespace Glyphs;

HomeScreen::HomeScreen(PDAController& pda) : Screen(pda) {
    name = "HOME";
    macro_index = 0;
    for (auto& row : tile_highlighted_) row.fill(false);
}

void HomeScreen::Render() {
    RenderFrame("YIP OS");
    RenderContent();
    RenderStatusBar();
    Logger::Debug("Home screen rendered");
}

void HomeScreen::RenderContent() {
    for (int ty = 0; ty < TILE_ROWS; ty++) {
        int row = ZONE_ROWS[ty];
        display_.WriteGlyph(0, row, G_VLINE);
        for (int tx = 0; tx < TILE_COLS; tx++) {
            WriteTile(tx, ty);
        }
        display_.WriteGlyph(COLS - 1, row, G_VLINE);
    }
}

void HomeScreen::RenderDynamic() {
    // VRCX tile (1,0): show "*" indicator when unseen notifications exist
    // "VRCX" at cols 2-5, row 4 → "*" at col 6
    if (pda_.HasUnseenNotifs()) {
        display_.WriteChar(6, ZONE_ROWS[1], static_cast<int>('*') + INVERT_OFFSET);
    } else {
        display_.WriteChar(6, ZONE_ROWS[1], static_cast<int>(' '));
    }

    RenderClock();
    RenderCursor();
}

void HomeScreen::WriteTile(int tx, int ty) {
    const char* label = TILE_LABELS[ty][tx].text;
    bool is_active = label[0] != '-';
    bool inverted = is_active && !tile_highlighted_[ty][tx];

    // VRCX tile (1,0) gets "*" suffix when unseen notifications exist
    std::string label_str;
    if (ty == 1 && tx == 0 && pda_.HasUnseenNotifs()) {
        label_str = std::string(label) + "*";
    } else {
        label_str = label;
    }

    int len = static_cast<int>(label_str.size());
    int center = TILE_CENTERS[tx];
    int start_col = center - len / 2;
    int row = ZONE_ROWS[ty];

    for (int i = 0; i < len; i++) {
        int c = start_col + i;
        if (c < 1 || c >= COLS - 1) continue;
        char ch = label_str[i];
        int char_idx = (ch >= 32 && ch <= 126) ? static_cast<int>(ch) : 32;
        if (inverted) char_idx += INVERT_OFFSET;
        display_.WriteChar(c, row, char_idx);
    }
}

bool HomeScreen::OnInput(const std::string& key) {
    if (key.size() == 2 && key[0] >= '1' && key[0] <= '5' && key[1] >= '1' && key[1] <= '3') {
        int tx = key[0] - '1';
        int ty = key[1] - '1';

        if (tx >= 0 && tx < TILE_COLS && ty >= 0 && ty < TILE_ROWS) {
            const char* label = TILE_LABELS[ty][tx].text;
            if (label[0] == '-') {
                Logger::Debug("Tile (" + std::to_string(tx) + "," + std::to_string(ty) + ") is empty");
                return true;
            }

            // Highlight tile — force immediate writes for flash visibility
            display_.CancelBuffered();
            tile_highlighted_[ty][tx] = true;
            WriteTile(tx, ty);
            Logger::Info("Tile (" + std::to_string(tx) + "," + std::to_string(ty) +
                        ") '" + label + "' -> navigating");

            pda_.SetPendingNavigate(label);
            return true;
        }
    }
    return false;
}

void HomeScreen::Update() {}

} // namespace YipOS
