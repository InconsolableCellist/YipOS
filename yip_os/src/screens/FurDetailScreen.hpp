#pragma once

#include "Screen.hpp"

#include <string>
#include <vector>

namespace YipOS {

struct FurEvent;

// Detail view for a single Furality event. SEL toggles the "marked" flag,
// which controls whether PollFurality fires a notification 15 min before
// the event starts. ML/BL paginate through long descriptions.
class FurDetailScreen : public Screen {
public:
    FurDetailScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

private:
    void RenderHeader();
    void RenderDescription();
    void RenderMarkIndicator();
    void RenderPageIndicator();
    std::string FormatTimeRange() const;
    void BuildDescriptionLines();
    int FirstPageDescRows() const { return 3; }   // rows 4-6 on page 0
    int LaterPageDescRows() const { return 6; }   // rows 1-6 on pages 1+
    int PageCount() const;
    int LinesOnPage(int page) const;
    int LineOffsetForPage(int page) const;

    const FurEvent* ev_ = nullptr;
    std::vector<std::string> desc_lines_;
    int page_ = 0;
};

} // namespace YipOS
