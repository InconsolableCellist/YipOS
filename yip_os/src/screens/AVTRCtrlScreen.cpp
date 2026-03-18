#include "AVTRCtrlScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "net/VRCAvatarData.hpp"
#include "net/OSCManager.hpp"
#include "core/Logger.hpp"
#include <cstdio>
#include <algorithm>
#include <cstring>

namespace YipOS {

using namespace Glyphs;

// Strip VRCFury prefix from param names for display
// Handles both "VF###/Path/Toggle" and "VF###_Path/Toggle" patterns
// For underscore variant like "VF127_Accessories/PDA/Enable", strips up to first '/'
static std::string StripVFPrefix(const std::string& name) {
    if (name.size() >= 4 && name[0] == 'V' && name[1] == 'F') {
        size_t i = 2;
        while (i < name.size() && name[i] >= '0' && name[i] <= '9') i++;
        if (i > 2 && i < name.size()) {
            if (name[i] == '/') {
                return name.substr(i + 1);
            }
            if (name[i] == '_') {
                // VF###_Category/Sub/Name → strip to after first '/'
                size_t slash = name.find('/', i + 1);
                if (slash != std::string::npos) {
                    return name.substr(slash + 1);
                }
                // No slash after underscore — just strip VF###_
                return name.substr(i + 1);
            }
        }
    }
    return name;
}

AVTRCtrlScreen::AVTRCtrlScreen(PDAController& pda) : Screen(pda) {
    name = "CTRL";
    macro_index = 19;
    LoadData();
}

void AVTRCtrlScreen::LoadData() {
    auto* data = pda_.GetAvatarData();
    if (!data) return;

    // Auto-select first avatar if none is current
    if (data->GetCurrentAvatarId().empty() && !data->GetAvatars().empty()) {
        data->SetCurrentAvatarId(data->GetAvatars().front().id);
    }

    auto& current_id = data->GetCurrentAvatarId();
    if (current_id.empty()) return;

    auto params = data->GetToggleParams(current_id);
    toggles_.clear();
    toggles_.reserve(params.size());
    for (auto* p : params) {
        toggles_.push_back({p, false});
    }
}

int AVTRCtrlScreen::PageCount() const {
    int n = static_cast<int>(toggles_.size());
    if (n == 0) return 1;
    return (n + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
}

int AVTRCtrlScreen::ItemCountOnPage() const {
    if (toggles_.empty()) return 0;
    int base = page_ * ROWS_PER_PAGE;
    int remaining = static_cast<int>(toggles_.size()) - base;
    return std::min(remaining, ROWS_PER_PAGE);
}

void AVTRCtrlScreen::RenderRow(int i, bool selected) {
    auto& d = display_;
    int idx = page_ * ROWS_PER_PAGE + i;
    int row = 1 + i;
    if (idx >= static_cast<int>(toggles_.size())) return;

    auto& t = toggles_[idx];

    // Format: "+ParamName" or "-ParamName"
    char indicator = t.on ? '+' : '-';
    int content_width = COLS - 2;
    std::string pname = StripVFPrefix(t.param->name);
    if (static_cast<int>(pname.size()) > content_width - 1) {
        pname = pname.substr(0, content_width - 1);
    }

    std::string line;
    line += indicator;
    line += pname;

    for (int c = 0; c < static_cast<int>(line.size()); c++) {
        int ch = static_cast<int>(line[c]);
        if (selected && c < SEL_WIDTH) ch += INVERT_OFFSET;
        d.WriteChar(1 + c, row, ch);
    }
}

void AVTRCtrlScreen::RenderRows() {
    for (int i = 0; i < ROWS_PER_PAGE; i++) {
        int idx = page_ * ROWS_PER_PAGE + i;
        if (idx >= static_cast<int>(toggles_.size())) break;
        RenderRow(i, i == cursor_);
    }
}

void AVTRCtrlScreen::RefreshCursorRows(int old_cursor, int new_cursor) {
    display_.CancelBuffered();
    display_.BeginBuffered();
    if (old_cursor != new_cursor && old_cursor >= 0 && old_cursor < ItemCountOnPage()) {
        RenderRow(old_cursor, false);
    }
    if (new_cursor >= 0 && new_cursor < ItemCountOnPage()) {
        RenderRow(new_cursor, true);
    }
    RenderPageIndicators();
}

void AVTRCtrlScreen::RenderPageIndicators() {
    auto& d = display_;
    if (!toggles_.empty()) {
        int global_idx = page_ * ROWS_PER_PAGE + cursor_ + 1;
        int total = static_cast<int>(toggles_.size());
        char pos[12];
        std::snprintf(pos, sizeof(pos), "%d/%d", global_idx, total);
        d.WriteText(5, 7, pos);
    }

    if (PageCount() <= 1) return;
    if (page_ > 0) {
        d.WriteGlyph(0, 3, G_UP);
    }
    if (page_ < PageCount() - 1) {
        d.WriteGlyph(0, 5, G_DOWN);
    }
}

void AVTRCtrlScreen::Render() {
    RenderFrame("CTRL");

    if (toggles_.empty()) {
        auto* data = pda_.GetAvatarData();
        if (!data || data->GetCurrentAvatarId().empty()) {
            display_.WriteText(2, 2, "No avatar selected");
            display_.WriteText(2, 3, "Use CHANGE first");
        } else {
            display_.WriteText(2, 2, "No toggles found");
        }
    } else {
        RenderRows();
    }

    RenderStatusBar();
}

void AVTRCtrlScreen::RenderDynamic() {
    if (!toggles_.empty()) {
        RenderRows();
        RenderPageIndicators();
    }
    RenderClock();
    RenderCursor();
}

bool AVTRCtrlScreen::OnInput(const std::string& key) {
    if (toggles_.empty()) return false;

    if (key == "Joystick") {
        int items = ItemCountOnPage();
        if (items == 0) return true;
        int old_cursor = cursor_;
        cursor_ = (cursor_ + 1) % items;
        RefreshCursorRows(old_cursor, cursor_);
        return true;
    }

    if (key == "TR") {
        // Toggle the selected parameter
        int idx = page_ * ROWS_PER_PAGE + cursor_;
        if (idx < static_cast<int>(toggles_.size())) {
            auto& t = toggles_[idx];
            t.on = !t.on;

            auto* osc = pda_.GetOSCManager();
            if (osc && t.param) {
                // Send matching type — VRChat ignores mismatched OSC types
                if (t.param->input_type == "Int") {
                    osc->SendInt(t.param->input_address, t.on ? 1 : 0);
                } else if (t.param->input_type == "Float") {
                    osc->SendFloat(t.param->input_address, t.on ? 1.0f : 0.0f);
                } else {
                    osc->SendBool(t.param->input_address, t.on);
                }
                Logger::Info("Toggle " + t.param->name + " (" + t.param->input_type +
                             ") = " + (t.on ? "ON" : "OFF"));
            }

            // Re-render just this row (keeps page position)
            display_.CancelBuffered();
            display_.BeginBuffered();
            RenderRow(cursor_, true);
            RenderPageIndicators();
        }
        return true;
    }

    if (key == "ML" && PageCount() > 1 && page_ > 0) {
        page_--;
        cursor_ = 0;
        pda_.StartRender(this);
        return true;
    }
    if (key == "BL" && PageCount() > 1 && page_ < PageCount() - 1) {
        page_++;
        cursor_ = 0;
        pda_.StartRender(this);
        return true;
    }

    return false;
}

} // namespace YipOS
