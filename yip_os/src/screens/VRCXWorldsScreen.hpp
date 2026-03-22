#pragma once

#include "Screen.hpp"
#include "net/VRCXData.hpp"
#include <vector>

namespace YipOS {

class VRCXWorldsScreen : public Screen {
public:
    VRCXWorldsScreen(PDAController& pda);

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
    static std::string FormatDuration(int64_t seconds);

    std::vector<VRCXWorldEntry> worlds_;
    int page_ = 0;
    int cursor_ = 0;  // selected row within current page (0 to ROWS_PER_PAGE-1)
    static constexpr int ROWS_PER_PAGE = 6;
    int PageCount() const;
    int ItemCountOnPage() const;
};

} // namespace YipOS
