#pragma once

#include "ListScreen.hpp"

#include <vector>

namespace YipOS {

struct FurEvent;

// Lists events for the day chosen on FuralityScreen (or ALL / MARKED).
// Selecting a row pushes FUR_DTL with the chosen event.
class FurListScreen : public ListScreen {
public:
    FurListScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;

protected:
    int ItemCount() const override;
    void RenderRow(int i, bool selected) override;
    void WriteSelectionMark(int i, bool selected) override;
    void RenderEmpty() override;
    bool OnSelect(int index) override;

private:
    void SyncEvents();
    std::string TitleForDay() const;

    int fur_day_ = -1;
    std::vector<const FurEvent*> events_;
};

} // namespace YipOS
