#pragma once

#include "Screen.hpp"

namespace YipOS {

class LockScreen : public Screen {
public:
    LockScreen(PDAController& pda);

    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

private:
    void RenderLockState();

    int sel_count_ = 0;
    double last_sel_time_ = 0;
    static constexpr double SEL_WINDOW = 2.0;  // 2s window for 3 presses
    static constexpr int UNLOCK_PRESSES = 3;
    bool unlocked_ = false;
};

} // namespace YipOS
