#pragma once

#include "ListScreen.hpp"

namespace YipOS {

struct FurEvent;

// Day picker: top-level FUR home. Lists "ALL", per-day rows, and "MARKED".
// When the user has a marked event upcoming, a "NEXT 3h 12m  Title" row
// appears at the top — selecting it jumps straight to that event's detail.
class FuralityScreen : public ListScreen {
public:
    static constexpr int kFurDayAll    = -1;
    static constexpr int kFurDayMarked = -2;
    static constexpr int kFurDayNext   = -3;

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
    bool HasNextRow() const { return next_event_ != nullptr; }
    void RecomputeNextEvent();
    static void FormatRelative(int64_t seconds, char* out, size_t n);

    int day_count_cached_ = 0;
    const FurEvent* next_event_ = nullptr;
    int64_t next_event_recomputed_at_ = 0;
};

} // namespace YipOS
