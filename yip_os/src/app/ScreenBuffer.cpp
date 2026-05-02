#include "ScreenBuffer.hpp"

namespace YipOS {

ScreenBuffer::ScreenBuffer() {
    Clear();
}

void ScreenBuffer::Put(int col, int row, int char_idx) {
    if (col >= 0 && col < Glyphs::COLS && row >= 0 && row < Glyphs::ROWS) {
        grid_[row][col] = static_cast<int16_t>(char_idx);
    }
}

int ScreenBuffer::Get(int col, int row) const {
    if (col >= 0 && col < Glyphs::COLS && row >= 0 && row < Glyphs::ROWS) {
        return grid_[row][col];
    }
    return UNKNOWN;
}

void ScreenBuffer::Clear() {
    for (auto& row : grid_) {
        row.fill(static_cast<int16_t>(' '));
    }
}

void ScreenBuffer::Invalidate() {
    for (auto& row : grid_) {
        row.fill(static_cast<int16_t>(UNKNOWN));
    }
}

} // namespace YipOS
