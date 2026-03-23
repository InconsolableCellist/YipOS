#include "ConfScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace YipOS {

using namespace Glyphs;

// Layout: inverted labels (buttons) on rows 1,4 aligned with touch contacts.
// Plain text values on rows 2,5 underneath.
// Each page has its own macro slot (labels baked in).
// ML = page up, BL = page down. Arrows on left border at rows 4,7.

// Find the index of the closest float value in a setting's values array
static int FindClosest(const std::vector<float>& values, float target, int from = 0) {
    int best = from;
    float best_dist = 999;
    for (int i = from; i < static_cast<int>(values.size()); i++) {
        float d = std::abs(values[i] - target);
        if (d < best_dist) { best_dist = d; best = i; }
    }
    return best;
}

ConfScreen::ConfScreen(PDAController& pda) : Screen(pda) {
    name = "CONF";
    macro_index = MACRO_BASE; // page 0 → slot 6

    auto& config = pda_.GetConfig();

    // === Page 1 (macro slot 6) ===
    // Row 1: BOOT, WRITE, SETTL
    settings_.push_back({"BOOT", "", {"OFF", "FAST", "NORM", "SLOW"},
                         {0.0f, 0.5f, 1.0f, 2.0f}, 0, false});
    settings_.back().current = config.boot_animation
        ? FindClosest(settings_.back().values, config.boot_speed, 1) : 0;

    settings_.push_back({"WRITE", "", {"ULTR", "FAST", "NORM", "SLOW"},
                         {0.02f, 0.04f, 0.07f, 0.12f}, 0, false});
    settings_.back().current = FindClosest(settings_.back().values, config.write_delay);

    settings_.push_back({"SETTL", "", {"FAST", "NORM", "SLOW"},
                         {0.02f, 0.04f, 0.08f}, 0, false});
    settings_.back().current = FindClosest(settings_.back().values, config.settle_delay);

    // Row 2: LOG, DBNCE, NVRAM
    settings_.push_back({"LOG", "", {"DBG", "INFO", "WARN", "ERR"},
                         {}, 0, false});
    {
        std::string lvl = config.log_level;
        if (lvl == "DEBUG")        settings_.back().current = 0;
        else if (lvl == "WARNING") settings_.back().current = 2;
        else if (lvl == "ERROR")   settings_.back().current = 3;
        else                       settings_.back().current = 1;
    }

    settings_.push_back({"DBNCE", "input.debounce", {"SHRT", "NORM", "LONG"},
                         {150.0f, 300.0f, 500.0f}, 0, false});
    settings_.back().current = FindClosest(settings_.back().values,
        std::stof(config.GetState("input.debounce", "300")));

    settings_.push_back({"NVRAM", "", {"CLR"}, {}, 0, true});

    // === Page 2 (macro slot 7) ===
    // Row 1: REFR, ALOCK, CHATBG — Row 2: REBOOT
    settings_.push_back({"REFR", "", {"OFF", "5S", "10S", "30S"},
                         {0.0f, 5.0f, 10.0f, 30.0f}, 0, false});
    settings_.back().current = FindClosest(settings_.back().values, config.refresh_interval);

    settings_.push_back({"ALOCK", "lock.autolock", {"OFF", "10S", "30S", "1M"},
                         {0.0f, 10.0f, 30.0f, 60.0f}, 0, false});
    settings_.back().current = FindClosest(settings_.back().values,
        std::stof(config.GetState("lock.autolock", "30")));

    settings_.push_back({"CHATBG", "chat.refresh", {"30S", "1M", "5M", "OFF"},
                         {30.0f, 60.0f, 300.0f, 0.0f}, 1, false});
    settings_.back().current = FindClosest(settings_.back().values,
        std::stof(config.GetState("chat.refresh", "60")));

    settings_.push_back({"REBOOT", "", {"GO!"}, {}, 0, true});
}

int ConfScreen::PageCount() const {
    return (static_cast<int>(settings_.size()) + SETTINGS_PER_PAGE - 1) / SETTINGS_PER_PAGE;
}

void ConfScreen::Render() {
    // Fallback for non-macro path
    RenderFrame("CONFIG");
    RenderValues();
    RenderPageIndicators();
    RenderStatusBar();
}

void ConfScreen::RenderDynamic() {
    // Values + page indicators + clock + cursor over macro background
    RenderValues();
    RenderPageIndicators();
    RenderClock();
    RenderCursor();
}

void ConfScreen::RenderValues() {
    int base = page_ * SETTINGS_PER_PAGE;
    auto& d = display_;

    // Check NVRAM confirmation / done timeout
    if (nvram_confirming_ || nvram_done_) {
        double now = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        double timeout = nvram_done_ ? NVRAM_DONE_TIMEOUT : NVRAM_CONFIRM_TIMEOUT;
        if ((now - nvram_confirm_time_) >= timeout) {
            nvram_confirming_ = false;
            nvram_done_ = false;
            refresh_interval = 0; // stop periodic refresh
            // Value will revert to "CLR" via normal GetSettingValue below
        }
    }

    // Row 2: plain text values under touch row 1 labels
    for (int i = 0; i < 3; i++) {
        int si = base + i;
        if (si >= static_cast<int>(settings_.size())) break;
        std::string val = PadValue(GetSettingValue(si));
        d.WriteText(BTN_COLS[i] - 2, 2, val);
    }

    // Row 5: plain text values under touch row 2 labels
    for (int i = 0; i < 3; i++) {
        int si = base + 3 + i;
        if (si >= static_cast<int>(settings_.size())) break;
        std::string val = PadValue(GetSettingValue(si));
        d.WriteText(BTN_COLS[i] - 2, 5, val);
    }
}

void ConfScreen::RenderPageIndicators() {
    if (PageCount() <= 1) return;
    auto& d = display_;

    // Up arrow on left border row 3 if can page up
    if (page_ > 0) {
        d.WriteGlyph(0, 3, G_UP);
    }

    // Down arrow on left border row 5 if can page down
    if (page_ < PageCount() - 1) {
        d.WriteGlyph(0, 5, G_DOWN);
    }

    // Page indicator "n/x" on row 7 after spinner (col 3)
    char pg[8];
    std::snprintf(pg, sizeof(pg), "%d/%d", page_ + 1, PageCount());
    d.WriteText(5, 7, pg);
}

std::string ConfScreen::PadValue(const std::string& value) {
    std::string val4 = value;
    while (val4.size() < 4) val4 += ' ';
    if (val4.size() > 4) val4 = val4.substr(0, 4);
    return val4;
}

std::string ConfScreen::GetSettingValue(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(settings_.size())) return "----";
    auto& s = settings_[idx];
    if (s.label == "NVRAM" && nvram_done_) {
        return "DONE";
    }
    if (s.label == "NVRAM" && nvram_confirming_) {
        return "SUR?";
    }
    if (s.current >= 0 && s.current < static_cast<int>(s.presets.size())) {
        return s.presets[s.current];
    }
    return "----";
}

void ConfScreen::CycleSetting(int setting_idx) {
    if (setting_idx < 0 || setting_idx >= static_cast<int>(settings_.size())) return;
    auto& s = settings_[setting_idx];

    if (s.is_action) return;

    s.current = (s.current + 1) % static_cast<int>(s.presets.size());
    ApplySetting(setting_idx);
}

void ConfScreen::ApplySetting(int setting_idx) {
    if (setting_idx < 0 || setting_idx >= static_cast<int>(settings_.size())) return;
    auto& s = settings_[setting_idx];
    auto& config = pda_.GetConfig();

    if (s.label == "BOOT") {
        if (s.current == 0) {
            config.boot_animation = false;
        } else {
            config.boot_animation = true;
            config.boot_speed = s.values[s.current];
        }
        config.SaveToFile(config.config_path);
        Logger::Info("Boot: " + s.presets[s.current] +
                     (s.current > 0 ? " (speed=" + std::to_string(config.boot_speed) + ")" : ""));
    }
    else if (s.label == "WRITE") {
        float val = s.values[s.current];
        config.write_delay = val;
        display_.SetWriteDelay(val);
        config.SaveToFile(config.config_path);
        Logger::Info("Write delay: " + std::to_string(val) + "s");
    }
    else if (s.label == "SETTL") {
        float val = s.values[s.current];
        config.settle_delay = val;
        display_.SetSettleDelay(val);
        config.SaveToFile(config.config_path);
        Logger::Info("Settle delay: " + std::to_string(val) + "s");
    }
    else if (s.label == "LOG") {
        static const char* levels[] = {"DEBUG", "INFO", "WARNING", "ERROR"};
        if (s.current >= 0 && s.current < 4) {
            config.log_level = levels[s.current];
            Logger::SetLogLevel(Logger::StringToLevel(config.log_level));
            config.SaveToFile(config.config_path);
            Logger::Info("Log level: " + config.log_level);
        }
    }
    else if (s.label == "DBNCE") {
        float val = s.values[s.current];
        config.SetState("input.debounce", std::to_string(static_cast<int>(val)));
        Logger::Info("Debounce: " + std::to_string(static_cast<int>(val)) + "ms");
    }
    else if (s.label == "REFR") {
        float val = s.values[s.current];
        config.refresh_interval = val;
        config.SaveToFile(config.config_path);
        Logger::Info("Refresh interval: " + std::to_string(static_cast<int>(val)) + "s");
    }
    else if (s.label == "ALOCK") {
        float val = s.values[s.current];
        config.SetState("lock.autolock", std::to_string(static_cast<int>(val)));
        pda_.ResetAutolockTimer();
        Logger::Info("Autolock: " + s.presets[s.current]);
    }
    else if (s.label == "CHATBG") {
        float val = s.values[s.current];
        config.SetState("chat.refresh", std::to_string(static_cast<int>(val)));
        Logger::Info("Chat refresh: " + s.presets[s.current]);
    }
}

bool ConfScreen::OnInput(const std::string& key) {
    // Pagination via ML (page up) and BL (page down)
    if (key == "ML" && PageCount() > 1 && page_ > 0) {
        page_--;
        macro_index = MACRO_BASE + page_;
        pda_.StartRender(this);
        Logger::Info("Config page up: " + std::to_string(page_ + 1));
        return true;
    }
    if (key == "BL" && PageCount() > 1 && page_ < PageCount() - 1) {
        page_++;
        macro_index = MACRO_BASE + page_;
        pda_.StartRender(this);
        Logger::Info("Config page down: " + std::to_string(page_ + 1));
        return true;
    }

    // Touch input: "XY" where X=col(1-5), Y=row(1-3)
    if (key.size() != 2) return false;
    int tx = key[0] - '1';
    int ty = key[1] - '1';

    // Map touch columns 1,3,5 to button indices 0,1,2
    int btn_idx = -1;
    if (tx == 0) btn_idx = 0;
    else if (tx == 2) btn_idx = 1;
    else if (tx == 4) btn_idx = 2;

    if (btn_idx < 0 || ty < 0 || ty > 1) return false;

    int base = page_ * SETTINGS_PER_PAGE;
    int setting_idx = base + (ty * 3) + btn_idx;

    if (setting_idx >= static_cast<int>(settings_.size())) return false;

    auto& s = settings_[setting_idx];
    auto& d = display_;

    // Label position (inverted button on rows 1,4)
    int label_row = (ty == 0) ? 1 : 4;
    int col_center = BTN_COLS[btn_idx];
    int lstart = col_center - static_cast<int>(s.label.size()) / 2;

    // Value position (plain text on rows 2,5)
    int value_row = (ty == 0) ? 2 : 5;

    if (s.is_action) {
        // Flash label: un-invert
        d.WriteText(lstart, label_row, s.label, false);

        if (s.label == "NVRAM") {
            double now = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();

            if (nvram_confirming_ && (now - nvram_confirm_time_) < NVRAM_CONFIRM_TIMEOUT) {
                // Second tap — actually clear
                pda_.GetConfig().ClearState();
                Logger::Info("NVRAM cleared via CONFIG screen");
                d.WriteText(lstart, label_row, s.label, true);
                d.WriteText(col_center - 2, value_row, "DONE");
                nvram_confirming_ = false;
                nvram_done_ = true;
                nvram_confirm_time_ = now; // reuse timer for DONE display
                refresh_interval = 1.0f;   // keep ticking for DONE timeout
            } else {
                // First tap — show confirmation
                nvram_confirming_ = true;
                nvram_done_ = false;
                nvram_confirm_time_ = now;
                refresh_interval = 1.0f;   // enable periodic refresh for timeout
                d.WriteText(lstart, label_row, s.label, true);
                d.WriteText(col_center - 2, value_row, "SUR?");
            }
        } else if (s.label == "REBOOT") {
            Logger::Info("Reboot requested via CONFIG screen");
            d.WriteText(lstart, label_row, s.label, true);
            d.WriteText(col_center - 2, value_row, "... ");
            pda_.Reboot();
        }
        return true;
    }

    // Flash label: un-invert
    d.WriteText(lstart, label_row, s.label, false);

    // Cycle to next preset
    CycleSetting(setting_idx);

    // Re-invert label
    d.WriteText(lstart, label_row, s.label, true);

    // Update value (plain text)
    d.WriteText(col_center - 2, value_row, PadValue(GetSettingValue(setting_idx)));

    return true;
}

} // namespace YipOS
