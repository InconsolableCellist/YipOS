#include "ScreenBuffer.hpp"
#include <sstream>

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

std::string ScreenBuffer::Dump() const {
    std::ostringstream ss;
    ss << "+" << std::string(Glyphs::COLS, '-') << "+\n";
    for (const auto& row : grid_) {
        ss << "|";
        for (char ch : row) ss << ch;
        ss << "|\n";
    }
    ss << "+" << std::string(Glyphs::COLS, '-') << "+";
    return ss.str();
}

} // namespace YipOS
