#pragma once

#include "Screen.hpp"
#include "core/Glyphs.hpp"
#include <array>

namespace YipOS {

class HomeScreen : public Screen {
public:
    HomeScreen(PDAController& pda);

    void Render() override;
    void RenderContent() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;
    void Update() override;

private:
    void WriteTile(int tx, int ty);

    std::array<std::array<bool, Glyphs::TILE_COLS>, Glyphs::TILE_ROWS> tile_highlighted_{};
};

} // namespace YipOS
