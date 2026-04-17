/**
 * ShockScreen.cpp
 * V1.0.0
 *
 * Screen for controlling multiple shock devices via the ShockManager interface.
 *
 * By otter_oasis
 */

#pragma once

#include "Screen.hpp"

namespace YipOS {

class ShockScreen : public Screen {
public:
  ShockScreen(PDAController &pda);

  void Render() override;
  void RenderContent() override;
  void RenderDynamic() override;
  bool OnInput(const std::string &key) override;
  void Update() override;

private:
  void RenderModeSelection();
  void RenderShockerSelection();

  int selected_shocker_idx_ = 0;
  int mode_idx_ = 1; // 0=Shock, 1=Vibrate, 2=Sound
  float intensity_ = 25.0f;
  int duration_ms_ = 1000;

  bool show_success_flash_ = false;
  double flash_end_time_ = 0;

  static constexpr const char *MODES[] = {"SHOCK", "VIBE ", "SOUND"};
};

} // namespace YipOS
