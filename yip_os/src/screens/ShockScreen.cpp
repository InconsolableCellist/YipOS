/**
 * ShockScreen.cpp
 * V1.0.0
 *
 * Screen for controlling multiple shock devices via the ShockManager interface.
 *
 * By otter_oasis
 */

#include "ShockScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Config.hpp"
#include "core/Glyphs.hpp"
#include "core/TimeUtil.hpp"
#include "net/ShockManager.hpp"
#include <algorithm>
#include <cstdio>

namespace YipOS {

using namespace Glyphs;

ShockScreen::ShockScreen(PDAController &pda) : Screen(pda) {
  name = "SHOCK";
  macro_index = 48; // OPENSHOCK macro
}

void ShockScreen::Render() {
  // The OPENSHOCK title and layout is baked into the macro texture
  RenderContent();
  RenderStatusBar();
}

void ShockScreen::RenderContent() {
  RenderShockerSelection();
  RenderModeSelection();

  // Draw feedback if flashing, otherwise restore the default label area
  if (show_success_flash_) {
    display_.WriteText(25, 6, " [  SENT!  ] ", true);
  } else {
    display_.WriteText(25, 6, " [ EXECUTE ] ", true);
  }

  auto *manager = pda_.GetShockManager();
  bool available =
      manager && manager->HasAnyConfig() && !manager->GetShockers().empty();

  // Intensity Value
  char buf[16];
  if (available) {
    std::snprintf(buf, sizeof(buf), "%3.0f%%", intensity_);
  } else {
    std::snprintf(buf, sizeof(buf), "  -  ");
  }
  display_.WriteText(8, 4, buf);

  // Duration Value
  if (available) {
    std::snprintf(buf, sizeof(buf), "%1.1fs", duration_ms_ / 1000.0f);
  } else {
    std::snprintf(buf, sizeof(buf), "  -  ");
  }
  display_.WriteText(28, 4, buf, false);
}

void ShockScreen::RenderDynamic() {
  if (show_success_flash_ && MonotonicNow() > flash_end_time_) {
    show_success_flash_ = false;
  }

  RenderContent();
  RenderClock();
  RenderCursor();
}

void ShockScreen::RenderShockerSelection() {
  auto *manager = pda_.GetShockManager();
  if (!manager)
    return;

  if (!manager->HasAnyConfig()) {
    display_.WriteText(8, 1, "SETUP IN APP");
    return;
  }

  const auto &items = manager->GetShockers();
  std::string label = "NO DEVICES OR BAD AUTH";
  if (!items.empty()) {
    if (selected_shocker_idx_ >= static_cast<int>(items.size())) {
      selected_shocker_idx_ = 0;
    }
    label = items[selected_shocker_idx_].name;
  }

  display_.WriteGlyph(1, 1, G_TRACKER);

  // Name centered between arrows (cols 8 to 31), padded to 24 chars to clear
  // old text
  std::string padded = label.substr(0, 24);
  if (padded.length() < 24)
    padded.append(24 - padded.length(), ' ');
  display_.WriteText(8, 1, padded);
}

void ShockScreen::RenderModeSelection() {
  auto *manager = pda_.GetShockManager();

  // Status indicator at Column 0 (to the left of ACTIVE MODE label) on Row 3
  std::string status = "- ";
  if (manager && manager->HasAnyConfig()) {
    status = manager->IsHealthy() ? "  " : "! ";
  }
  display_.WriteText(2, 1, status);

  std::string val =
      (!manager || !manager->HasAnyConfig() || manager->GetShockers().empty())
          ? "UNAVAILABLE"
          : MODES[mode_idx_];

  // Add hazard markers if in SHOCK mode
  if (mode_idx_ == 0 && manager && manager->HasAnyConfig()) {
    val = "!!" + std::string(MODES[0]) + "!!";
  }

  // Pad with spaces to clear old text (e.g. switching from !!SHOCK!! to VIBE)
  if (val.length() < 11) {
    val += std::string(11 - val.length(), ' ');
  }

  // Positioned at Col 17 to sit directly after the new macro label position
  // with a space on Row 3
  display_.WriteText(17, 3, val);
}

bool ShockScreen::OnInput(const std::string &key) {
  if (key == "TL") {
    pda_.PopScreen();
    return true;
  }

  if (key.size() == 2 && key[0] >= '1' && key[0] <= '5' && key[1] >= '1' &&
      key[1] <= '3') {
    int tx = key[0] - '1';
    int ty = key[1] - '1';

    auto *manager = pda_.GetShockManager();
    auto &config = pda_.GetConfig();
    bool changed = false;

    if (ty == 0) { // Top Row: Devices (Unchanged)
      bool device_changed = false;
      if (tx == 0) { // Previous
        if (selected_shocker_idx_ > 0) {
          selected_shocker_idx_--;
          changed = true;
          device_changed = true;
        }
      } else if (tx == 4) { // Next
        if (manager &&
            selected_shocker_idx_ <
                static_cast<int>(manager->GetShockers().size()) - 1) {
          selected_shocker_idx_++;
          changed = true;
          device_changed = true;
        }
      }
      if (device_changed && manager && !manager->GetShockers().empty()) {
        const auto &s = manager->GetShockers()[selected_shocker_idx_];
        int min_d = manager->GetMinDurationMs(s.backend);
        int max_d = manager->GetMaxDurationMs(s.backend);
        duration_ms_ = std::clamp(duration_ms_, min_d, max_d);
      }
    } else if (ty == 1) { // Middle Row: Intensity (Left) & Duration (Right)
      float i_step = 2.5f;
      int d_step = 1000;
      try {
        i_step = std::stof(config.GetState("shock.intensity_step", "2.5"));
      } catch (...) {
      }
      try {
        d_step = std::stoi(config.GetState("shock.duration_step", "1000"));
      } catch (...) {
      }

      if (tx == 0) { // Int Down
        intensity_ = std::max(0.0f, intensity_ - i_step);
        changed = true;
      } else if (tx == 2) { // Int Up
        intensity_ = std::min(100.0f, intensity_ + i_step);
        changed = true;
      } else if (tx == 3 || tx == 4) { // Duration Down/Up
        int min_d = 100;
        int max_d = 30000;
        if (manager && !manager->GetShockers().empty()) {
          const auto &s = manager->GetShockers()[selected_shocker_idx_];
          min_d = manager->GetMinDurationMs(s.backend);
          max_d = manager->GetMaxDurationMs(s.backend);
        }
        if (tx == 3) {
          duration_ms_ = std::max(min_d, duration_ms_ - d_step);
        } else {
          duration_ms_ = std::min(max_d, duration_ms_ + d_step);
        }
        changed = true;
      }
    } else if (ty == 2) { // Bottom Row: Mode Cycle (Left) & Execute (Right)
      if (tx <= 1) {      // Mode (tx 0-1)
        mode_idx_ = (mode_idx_ + 1) % 3;
        changed = true;
      } else if (tx >= 3) { // EXECUTE (tx 3-4)
        if (manager && !manager->GetShockers().empty()) {
          const auto &s = manager->GetShockers()[selected_shocker_idx_];
          manager->SendControl(s.id, s.backend, MODES[mode_idx_], intensity_,
                               duration_ms_);
          show_success_flash_ = true;
          flash_end_time_ = MonotonicNow() + 2.0;
          changed = true;
        }
      }
    }

    if (changed) {
      display_.BeginBuffered();
      RenderContent();
    }
    return true;
  }

  return false;
}

void ShockScreen::Update() {
  static double last_fetch = 0;
  if (MonotonicNow() - last_fetch > 30.0) {
    auto *manager = pda_.GetShockManager();
    if (manager)
      manager->FetchShockers();
    last_fetch = MonotonicNow();
  }
}

} // namespace YipOS
