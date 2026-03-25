#pragma once

#include "Screen.hpp"

namespace YipOS {

class QRTestScreen : public Screen {
public:
    QRTestScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    void Update() override;
    bool OnInput(const std::string& key) override;

private:
    void StartQRRender();

    bool rendering_ = false;
    float saved_write_delay_ = 0.07f;
};

} // namespace YipOS
