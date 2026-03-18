#pragma once

#include <cstdint>
#include <array>
#include <string>

namespace YipOS {
namespace Glyphs {

// Grid constants
constexpr int COLS = 40;
constexpr int ROWS = 8;
constexpr int TILE_COLS = 5;
constexpr int TILE_ROWS = 3;
constexpr int CHARS_PER_TILE = COLS / TILE_COLS; // 8

// Column centers for even spacing across 40 cols (contact grid alignment)
// (2*i+1) * (COLS / (TILE_COLS*2)) for i in 0..4 → [4, 12, 20, 28, 36]
constexpr std::array<int, 5> TILE_CENTERS = {4, 12, 20, 28, 36};

// Tile labels [row][col]
constexpr int MAX_LABEL_LEN = 6;
struct TileLabel {
    char text[MAX_LABEL_LEN + 1];
};

// clang-format off
constexpr TileLabel TILE_LABELS[TILE_ROWS][TILE_COLS] = {
    {{"STATS"}, {"NET"},   {"-----"}, {"SPVR"},  {"CONF"}},
    {{"VRCX"},  {"HEART"}, {"BFI"},   {"DBG"},   {"-----"}},
    {{"CC"},    {"AVTR"},  {"-----"}, {"-----"}, {"LOCK"}},
};
// clang-format on

// Touch zone centers as integer rows (must match macro atlas layout)
// Python: ZONE_ROWS = [round(ROWS * k / 6 - 0.5) for k in (1, 3, 5)] → [1, 4, 6]
constexpr std::array<int, 3> ZONE_ROWS = {1, 4, 6};

// Inversion offset: add to any ASCII char (32-127) to get inverted variant
constexpr int INVERT_OFFSET = 128;

// OSC parameter prefix
constexpr const char* PARAM_PREFIX = "/avatar/parameters/";

// --- PDA ROM Glyph Indices ---
// Box-drawing (0-11)
constexpr uint8_t G_EMPTY     = 0;
constexpr uint8_t G_HLINE     = 1;   // ─
constexpr uint8_t G_VLINE     = 2;   // │
constexpr uint8_t G_TL_CORNER = 3;   // ┌
constexpr uint8_t G_TR_CORNER = 4;   // ┐
constexpr uint8_t G_BL_CORNER = 5;   // └
constexpr uint8_t G_BR_CORNER = 6;   // ┘
constexpr uint8_t G_L_TEE     = 7;   // ├
constexpr uint8_t G_R_TEE     = 8;   // ┤
constexpr uint8_t G_T_TEE     = 9;   // ┬
constexpr uint8_t G_B_TEE     = 10;  // ┴
constexpr uint8_t G_CROSS     = 11;  // ┼

// Block elements (12-19)
constexpr uint8_t G_SOLID     = 12;  // █
constexpr uint8_t G_UPPER     = 13;  // ▀
constexpr uint8_t G_LOWER     = 14;  // ▄
constexpr uint8_t G_LEFT      = 15;  // ▌
constexpr uint8_t G_RIGHT     = 16;  // ▐
constexpr uint8_t G_SHADE1    = 17;  // ░ 25%
constexpr uint8_t G_SHADE2    = 18;  // ▒ 50%
constexpr uint8_t G_SHADE3    = 19;  // ▓ 75%

// Symbols (20-29)
constexpr uint8_t G_BULLET    = 20;  // ●
constexpr uint8_t G_HEART     = 21;  // ♥
constexpr uint8_t G_NOTE      = 22;  // ♪
constexpr uint8_t G_DNOTE     = 23;  // ♫
constexpr uint8_t G_UP        = 24;  // ↑
constexpr uint8_t G_DOWN      = 25;  // ↓
constexpr uint8_t G_RIGHT_A   = 26;  // →
constexpr uint8_t G_LEFT_A    = 27;  // ←
constexpr uint8_t G_GEAR      = 28;  // ☼
constexpr uint8_t G_HOME      = 29;  // ⌂

// Custom PDA icons (128-143)
constexpr uint8_t G_SIGNAL     = 128;
constexpr uint8_t G_BATT_FULL  = 129;
constexpr uint8_t G_BATT_HALF  = 130;
constexpr uint8_t G_BATT_LOW   = 131;
constexpr uint8_t G_BATT_EMPTY = 132;
constexpr uint8_t G_LOCK       = 133;
constexpr uint8_t G_UNLOCK     = 134;
constexpr uint8_t G_SETTINGS   = 135;
constexpr uint8_t G_PLAY       = 136;
constexpr uint8_t G_PAUSE      = 137;
constexpr uint8_t G_SKIP_FWD   = 138;
constexpr uint8_t G_SKIP_BACK  = 139;
constexpr uint8_t G_WIFI       = 140;
constexpr uint8_t G_CHECK      = 141;
constexpr uint8_t G_XMARK      = 142;
constexpr uint8_t G_TRACKER    = 143;
constexpr uint8_t G_LOCK_INV   = 144;

// Boot macro glyph index
constexpr int BOOT_MACRO_INDEX = 5;

} // namespace Glyphs
} // namespace YipOS
