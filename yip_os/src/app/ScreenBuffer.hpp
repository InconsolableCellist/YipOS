#pragma once

#include <array>
#include <cstdint>
#include "core/Glyphs.hpp"

namespace YipOS {

// Mirrors the contents of the on-screen render texture, one entry per cell.
// Stores the raw glyph index (0-255) — not just the printable ASCII subset —
// so PDADisplay can skip writes whose target cell already holds the same
// glyph. UNKNOWN means "we don't know what's there" and forces a write.
class ScreenBuffer {
public:
    static constexpr int UNKNOWN = -1;

    ScreenBuffer();

    void Put(int col, int row, int char_idx);
    int Get(int col, int row) const;
    void Clear();
    // After a macro stamp every cell on the RT shows the macro's glyphs but
    // we don't track those per-cell. Mark every cell unknown so the next
    // overlapping write isn't suppressed.
    void Invalidate();
private:
    std::array<std::array<int16_t, Glyphs::COLS>, Glyphs::ROWS> grid_;
};

} // namespace YipOS
