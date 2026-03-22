#pragma once

#include "Screen.hpp"
#include "net/VRCXData.hpp"
#include <vector>

namespace YipOS {

class VRCXNotifScreen : public Screen {
public:
    VRCXNotifScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

private:
    void LoadData();
    void RenderRow(int i, bool selected);
    void RenderRows();
    void RefreshCursorRows(int old_cursor, int new_cursor);
    void WriteSelectionMark(int i, bool selected);
    void RenderPageIndicators();
    static std::string FormatTime(const std::string& created_at);
    static std::string FormatType(const std::string& type);

    void RenderClearButton();

    std::vector<VRCXNotifEntry> notifs_;
    std::string last_seen_at_;  // timestamp of last seen notification
    int page_ = 0;
    int cursor_ = 0;
    static constexpr int ROWS_PER_PAGE = 6;
    int PageCount() const;
    int ItemCountOnPage() const;

    // CLR ALL double-tap confirmation
    bool clear_confirming_ = false;
    bool clear_done_ = false;
    double clear_confirm_time_ = 0.0;
    static constexpr double CONFIRM_TIMEOUT = 3.0;
    static constexpr double DONE_TIMEOUT = 2.0;

    // Button position: centered on col 20 (touch 33)
    static constexpr int BTN_CENTER = 20;
};

} // namespace YipOS
