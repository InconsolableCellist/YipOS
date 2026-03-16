#pragma once

#include "Screen.hpp"
#include "net/VRCXData.hpp"
#include <vector>

namespace YipOS {

class VRCXFeedScreen : public Screen {
public:
    VRCXFeedScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

private:
    void LoadData();
    void RenderRow(int i, bool selected);
    void RenderRows();
    void RefreshCursorRows(int old_cursor, int new_cursor);
    void RenderPageIndicators();
    static std::string FormatTime(const std::string& created_at);

    std::vector<VRCXFeedEntry> feed_;
    int page_ = 0;
    int cursor_ = 0;
    static constexpr int ROWS_PER_PAGE = 6;
    int PageCount() const;
    int ItemCountOnPage() const;
};

} // namespace YipOS
