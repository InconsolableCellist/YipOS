#pragma once

#include "ListScreen.hpp"

namespace YipOS {

// Day picker: top-level FUR home. Lists "ALL", per-day rows, and "MARKED".
// Selecting a row stashes the day choice on PDAController and navigates to
// FUR_LIST.
class FuralityScreen : public ListScreen {
public:
    static constexpr int kFurDayAll    = -1;
    static constexpr int kFurDayMarked = -2;

    FuralityScreen(PDAController& pda);

    void Render() override;
    void Update() override;

protected:
    int ItemCount() const override;
    void RenderRow(int i, bool selected) override;
    void WriteSelectionMark(int i, bool selected) override;
    void RenderEmpty() override;
    bool OnSelect(int index) override;

private:
    int IndexToFurDay(int row) const;
    void RenderRowText(int row, bool selected);

    int day_count_cached_ = 0;
};

} // namespace YipOS
