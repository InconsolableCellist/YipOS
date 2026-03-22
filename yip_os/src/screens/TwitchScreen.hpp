#pragma once

#include "Screen.hpp"
#include "net/TwitchClient.hpp"
#include <vector>

namespace YipOS {

class TwitchScreen : public Screen {
public:
    TwitchScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;
    void Update() override;

private:
    // Render the featured message in the inverted block (rows 1-4)
    void RenderFeatured();
    // Render a single-row message on row 5 or 6
    void RenderOlderRow(int row, int msg_index, bool selected);
    // Render all visible content
    void RenderAll();
    // Write just the 3-char selection indicator for cursor changes
    void WriteSelectionMark(int slot, bool selected);
    void RenderPageIndicator();
    void RenderConnectionStatus();
    void SyncMessages();

    static std::string FormatRelativeTime(int64_t date);

    std::vector<TwitchMessage> messages_;
    int page_ = 0;
    int cursor_ = 0;  // 0=featured, 1=2nd, 2=3rd on current page
    static constexpr int FEED_MACRO = 33;
    static constexpr int MSGS_PER_PAGE = 3;  // 1 featured + 2 older
    static constexpr int SEL_WIDTH = 3;
    static constexpr int MAX_MESSAGES = 50;

    // New-message tracking
    uint64_t last_counter_ = 0;
    double last_redraw_time_ = 0;
    static constexpr double MIN_REDRAW_INTERVAL = 2.0;

    // Message index for a given slot on current page
    int MsgIndex(int slot) const { return page_ * MSGS_PER_PAGE + slot; }
    int PageCount() const;
    int VisibleOnPage() const;
};

} // namespace YipOS
