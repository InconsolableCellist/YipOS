#pragma once

#include "Screen.hpp"
#include "net/VRCAvatarData.hpp"
#include <vector>

namespace YipOS {

class AVTRChangeScreen : public Screen {
public:
    AVTRChangeScreen(PDAController& pda);

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
    int PageCount() const;
    int ItemCountOnPage() const;

    std::vector<VRCAvatarEntry> avatars_;
    int page_ = 0;
    int cursor_ = 0;
    static constexpr int ROWS_PER_PAGE = 6;
    static constexpr int SEL_WIDTH = 3;
};

} // namespace YipOS
