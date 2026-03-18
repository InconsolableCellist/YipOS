#include "LockScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Logger.hpp"
#include <chrono>

namespace YipOS {

using namespace Glyphs;

static double MonotonicNow() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

LockScreen::LockScreen(PDAController& pda) : Screen(pda) {
    name = "LOCK";
    macro_index = 21;
    pda_.SetLocked(true);
    Logger::Info("Screen locked");
}

void LockScreen::RenderDynamic() {
    RenderLockState();
    RenderClock();
    RenderCursor();
}

void LockScreen::RenderLockState() {
    if (unlocked_) {
        display_.WriteGlyph(16, 2, G_UNLOCK);
        display_.WriteText(18, 2, "UNLOCK");
    } else if (sel_count_ > 0) {
        // Show progress dots
        char progress[8];
        int i = 0;
        for (; i < sel_count_; i++) progress[i] = '*';
        for (; i < UNLOCK_PRESSES; i++) progress[i] = '-';
        progress[UNLOCK_PRESSES] = '\0';
        display_.WriteText(18, 5, progress);
    } else {
        display_.WriteText(18, 5, "   ");
    }
}

bool LockScreen::OnInput(const std::string& key) {
    if (unlocked_) return true;  // absorb all input while transitioning

    if (key == "TR") {
        double now = MonotonicNow();

        // Reset if too much time passed
        if (sel_count_ > 0 && (now - last_sel_time_) > SEL_WINDOW) {
            sel_count_ = 0;
        }

        sel_count_++;
        last_sel_time_ = now;

        display_.CancelBuffered();
        display_.BeginBuffered();

        if (sel_count_ >= UNLOCK_PRESSES) {
            unlocked_ = true;
            pda_.SetLocked(false);
            Logger::Info("Screen unlocked");
            RenderLockState();
            // Navigate back after brief visual feedback
            pda_.SetPendingNavigate("__POP__");
        } else {
            RenderLockState();
        }
        return true;
    }

    // Absorb all other input when locked
    return true;
}

} // namespace YipOS
