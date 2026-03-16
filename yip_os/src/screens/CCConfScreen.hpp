#pragma once

#include "Screen.hpp"
#include <string>

namespace YipOS {

class CCConfScreen : public Screen {
public:
    CCConfScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

private:
    void RenderContent();
    void WriteInverted(int col, int row, const std::string& text);
    void FlashButton(int col, int row, const std::string& text);
};

} // namespace YipOS
