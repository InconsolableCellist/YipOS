#pragma once

#include "Screen.hpp"

namespace YipOS {

struct FurEvent;

// Detail view for a single Furality event. TR toggles the "marked" flag,
// which controls whether PollFurality fires a notification 15 min before
// the event starts.
class FurDetailScreen : public Screen {
public:
    FurDetailScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

private:
    void RenderContent();
    void RenderMarkRow();
    std::string FormatTimeRange() const;

    const FurEvent* ev_ = nullptr;
};

} // namespace YipOS
