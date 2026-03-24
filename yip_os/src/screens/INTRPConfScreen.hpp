#pragma once

#include "Screen.hpp"
#include <string>

namespace YipOS {

class INTRPConfScreen : public Screen {
public:
    INTRPConfScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

private:
    void RenderContent();
    std::string GetLangLabel(const std::string& code) const;
};

} // namespace YipOS
