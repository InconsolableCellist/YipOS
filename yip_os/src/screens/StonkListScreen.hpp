#pragma once

#include "Screen.hpp"
#include <vector>
#include <string>

namespace YipOS {

class StonkListScreen : public Screen {
public:
    StonkListScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

private:
    void RenderRows();
    void RenderRow(int i, bool selected);
    void RefreshCursorRows(int old_cursor, int new_cursor);
    void WriteSelectionMark(int i, bool selected);
    void RenderPageIndicators();

    std::vector<std::string> symbols_;
    int page_ = 0;
    int cursor_ = 0;
    static constexpr int ROWS_PER_PAGE = 6;
    int PageCount() const;
    int ItemCountOnPage() const;
};

} // namespace YipOS
