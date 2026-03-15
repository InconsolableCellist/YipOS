#pragma once

#include "Screen.hpp"
#include <string>
#include <vector>

namespace YipOS {

class ConfScreen : public Screen {
public:
    ConfScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

private:
    void RenderValues();
    void RenderPageIndicators();
    void ApplySetting(int setting_idx);
    void CycleSetting(int setting_idx);
    std::string GetSettingValue(int setting_idx) const;
    static std::string PadValue(const std::string& value);

    struct Setting {
        std::string label;
        std::string config_key;
        std::vector<std::string> presets;
        std::vector<float> values;
        int current = 0;
        bool is_action = false;
    };

    std::vector<Setting> settings_;
    int page_ = 0;
    static constexpr int SETTINGS_PER_PAGE = 6;
    int PageCount() const;

    // Button column centers (touch cols 1,3,5 → TILE_CENTERS[0,2,4])
    static constexpr int BTN_COLS[3] = {4, 20, 36};

    // Macro indices per page (one atlas slot per config page)
    static constexpr int MACRO_BASE = 6;  // page 0 → slot 6, page 1 → slot 7, etc.
};

} // namespace YipOS
