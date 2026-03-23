#include "ScreenBuffer.hpp"

namespace YipOS {

ScreenBuffer::ScreenBuffer() {
    Clear();
}

void ScreenBuffer::Put(int col, int row, char ch) {
    if (col >= 0 && col < Glyphs::COLS && row >= 0 && row < Glyphs::ROWS) {
        grid_[row][col] = ch;
    }
}

char ScreenBuffer::Get(int col, int row) const {
    if (col >= 0 && col < Glyphs::COLS && row >= 0 && row < Glyphs::ROWS) {
        return grid_[row][col];
    }
    return ' ';
}

void ScreenBuffer::Clear() {
    for (auto& row : grid_) {
        row.fill(' ');
    }
}

} // namespace YipOS
