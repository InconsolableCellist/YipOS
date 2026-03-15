#pragma once

#include <array>
#include <cstdint>
#include <string>
#include "core/Glyphs.hpp"

namespace YipOS {

class ScreenBuffer {
public:
    ScreenBuffer();

    void Put(int col, int row, char ch);
    char Get(int col, int row) const;
    void Clear();
    std::string Dump() const;

private:
    std::array<std::array<char, Glyphs::COLS>, Glyphs::ROWS> grid_;
};

} // namespace YipOS
