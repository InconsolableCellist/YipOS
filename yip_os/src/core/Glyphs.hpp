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

// Tile labels [page][row][col]
constexpr int MAX_LABEL_LEN = 6;
constexpr int HOME_PAGES = 2;
struct TileLabel {
    char text[MAX_LABEL_LEN + 1];
};

// clang-format off
constexpr TileLabel TILE_LABELS[HOME_PAGES][TILE_ROWS][TILE_COLS] = {
    // Page 0
    {{{"STATS"}, {"NET"},   {"IMG"},   {"SPVR"},  {"CONF"}},
     {{"VRCX"},  {"HEART"}, {"BFI"},   {"STONK"}, {"CHAT"}},
     {{"CC"},    {"AVTR"},  {"TEXT"},  {"MEDIA"}, {"LOCK"}}},
    // Page 1
    {{{"DBG"},   {"TWTCH"}, {"INTRP"}, {"QRTEST"}, {"-----"}},
     {{"-----"}, {"-----"}, {"-----"}, {"-----"}, {"-----"}},
     {{"-----"}, {"-----"}, {"-----"}, {"-----"}, {"-----"}}},
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
constexpr uint8_t G_UPPER_SHADE = 145; // ▀ dithered (50%)
constexpr uint8_t G_LOWER_SHADE = 146; // ▄ dithered (50%)

// Boot macro glyph index
constexpr int BOOT_MACRO_INDEX = 5;

// --- Extended ROM (Bank 1) ---
// Bank 1 atlas layout:
//   0-31:    Accented Latin lowercase (àáâãäåæçèéêëìíîïðñòóôõöøùúûüýþßÿ)
//   32-61:   Accented Latin uppercase (ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖØÙÚÛÜÝÞ)
//   62-65:   Extra Latin (œŒ¿¡)
//   66-148:  Hiragana (ぁ-ん, U+3041-U+3093)
//   149-231: Katakana (ァ-ン, U+30A1-U+30F3)
//   232-238: Japanese punctuation (。、「」・ー～)
//   239-255: Reserved

struct BankedGlyph {
    int bank;   // 0 = standard ROM, 1 = extended ROM
    int index;  // glyph index within that bank (0-255)
};

// Decode one UTF-8 codepoint from a string, advancing pos.
// Returns the Unicode codepoint, or 0xFFFD on invalid input.
inline uint32_t DecodeUTF8(const std::string& s, size_t& pos) {
    if (pos >= s.size()) return 0xFFFD;
    uint8_t c = static_cast<uint8_t>(s[pos]);
    uint32_t cp;
    int extra;
    if (c < 0x80) { cp = c; extra = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else { pos++; return 0xFFFD; }
    pos++;
    for (int i = 0; i < extra; i++) {
        if (pos >= s.size() || (static_cast<uint8_t>(s[pos]) & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (static_cast<uint8_t>(s[pos]) & 0x3F);
        pos++;
    }
    return cp;
}

// Map a Unicode codepoint to a (bank, index) pair for display.
// Bank 0 = standard ROM (ASCII 32-127, UI primitives 0-31, icons 128-159, inverted 160-255).
// Bank 1 = extended ROM (accented Latin, hiragana, katakana, JP punctuation).
// Unmapped codepoints return bank 0 index 32 (space).
inline BankedGlyph MapCodepoint(uint32_t cp) {
    // Bank 0: ASCII (direct mapping)
    if (cp >= 32 && cp <= 126) return {0, static_cast<int>(cp)};

    // Bank 1: Accented Latin lowercase — indices 0-29
    // U+00E0-U+00F6 (skip ÷ U+00F7), U+00F8-U+00FE
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) {
        int idx = static_cast<int>(cp - 0xE0);
        if (cp > 0xF7) idx--;  // compensate for skipped ÷
        return {1, idx};
    }
    if (cp == 0xDF) return {1, 30};  // ß
    if (cp == 0xFF) return {1, 31};  // ÿ

    // Bank 1: Accented Latin uppercase — indices 32-61
    // U+00C0-U+00D6 (skip × U+00D7), U+00D8-U+00DE
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) {
        int idx = 32 + static_cast<int>(cp - 0xC0);
        if (cp > 0xD7) idx--;  // compensate for skipped ×
        return {1, idx};
    }

    // Bank 1: Extra Latin — indices 62-65
    if (cp == 0x0153) return {1, 62};  // œ
    if (cp == 0x0152) return {1, 63};  // Œ
    if (cp == 0x00BF) return {1, 64};  // ¿
    if (cp == 0x00A1) return {1, 65};  // ¡

    // Bank 1: Hiragana — indices 66-148 (U+3041-U+3093, 83 codepoints)
    if (cp >= 0x3041 && cp <= 0x3093) {
        return {1, 66 + static_cast<int>(cp - 0x3041)};
    }

    // Bank 1: Katakana — indices 149-231 (U+30A1-U+30F3, 83 codepoints)
    if (cp >= 0x30A1 && cp <= 0x30F3) {
        return {1, 149 + static_cast<int>(cp - 0x30A1)};
    }

    // Bank 1: Japanese punctuation — indices 232-238
    switch (cp) {
        case 0x3002: return {1, 232};  // 。
        case 0x3001: return {1, 233};  // 、
        case 0x300C: return {1, 234};  // 「
        case 0x300D: return {1, 235};  // 」
        case 0x30FB: return {1, 236};  // ・
        case 0x30FC: return {1, 237};  // ー
        case 0xFF5E: return {1, 238};  // ～ (fullwidth tilde)
    }

    // CJK Unified Ideographs: display '?' (kanji without MeCab conversion)
    if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF))
        return {0, '?'};

    // Fallback: space in bank 0
    return {0, 32};
}

} // namespace Glyphs
} // namespace YipOS
