#pragma once

#include <string>
#include <memory>

namespace YipOS {

class PDAController;
class PDADisplay;

class Screen {
public:
    std::string name = "screen";
    int macro_index = -1;
    float refresh_interval = 0.0f;
    float update_interval = 0.0f;
    bool skip_clock = false; // skip clock/cursor writes (for screens that own the full display)

    Screen(PDAController& pda);
    virtual ~Screen() = default;

    virtual void Render();
    virtual void RenderContent();
    virtual void RenderDynamic();
    virtual bool OnInput(const std::string& key);
    virtual void Update();

    void RenderFrame(const std::string& title);
    void RenderStatusBar();
    void RenderClock();
    void RenderCursor();
    void RenderStatusIcons();

protected:
    PDAController& pda_;
    PDADisplay& display_;
};

// Factory
std::unique_ptr<Screen> CreateScreen(const std::string& name, PDAController& pda);

} // namespace YipOS
