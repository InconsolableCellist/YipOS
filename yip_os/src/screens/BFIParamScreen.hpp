#pragma once

#include "Screen.hpp"

namespace YipOS {

class BFIParamScreen : public Screen {
public:
    BFIParamScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

private:
    void RenderRow(int i, bool selected);
    void RenderSelPrefix(int i, bool selected);
    void RenderRows();
    void RenderPageIndicators();
    void RefreshCursorRows(int old_cursor, int new_cursor);
    int PageCount() const;
    int ItemCountOnPage() const;

    int page_ = 0;
    int cursor_ = 0;
    int active_idx_ = 0;  // currently selected param (shown with "+")
    int max_name_len_ = 0;
    static constexpr int ROWS_PER_PAGE = 6;
    static constexpr int SEL_WIDTH = 3;
};

} // namespace YipOS
