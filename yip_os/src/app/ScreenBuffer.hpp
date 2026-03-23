#pragma once

#include <array>
#include "core/Glyphs.hpp"

namespace YipOS {

class ScreenBuffer {
public:
    ScreenBuffer();

    void Put(int col, int row, char ch);
    char Get(int col, int row) const;
    void Clear();
private:
    std::array<std::array<char, Glyphs::COLS>, Glyphs::ROWS> grid_;
};

} // namespace YipOS
