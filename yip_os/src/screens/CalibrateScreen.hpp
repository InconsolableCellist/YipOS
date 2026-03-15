#pragma once

#include "Screen.hpp"
#include <string>

namespace YipOS {

class CalibrateScreen : public Screen {
public:
    CalibrateScreen(PDAController& pda);

    void Render() override;
    void RenderContent() override;
    bool OnInput(const std::string& key) override;
};

} // namespace YipOS
