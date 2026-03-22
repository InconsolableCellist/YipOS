#pragma once

#include "Screen.hpp"
#include "net/ChatClient.hpp"
#include <vector>
#include <chrono>

namespace YipOS {

class ChatScreen : public Screen {
public:
    ChatScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

private:
    enum Mode { CONSENT, FEED };

    void RenderConsent();
    void RenderFeed();
    void RenderRow(int i, bool selected);
    void RenderRows();
    void RefreshCursorRows(int old_cursor, int new_cursor);
    void WriteSelectionMark(int i, bool selected);
    void RenderPageIndicators();

    static std::string FormatRelativeTime(int64_t date);

    Mode mode_ = CONSENT;
    std::vector<ChatMessage> messages_;
    int page_ = 0;
    int cursor_ = 0;
    static constexpr int ROWS_PER_PAGE = 6;
    static constexpr int CONSENT_MACRO = 24;
    static constexpr int FEED_MACRO = 25;
    static constexpr double CONSENT_DELAY_SEC = 2.0;

    int PageCount() const;
    int ItemCountOnPage() const;

    std::chrono::steady_clock::time_point consent_shown_at_{};
};

} // namespace YipOS
