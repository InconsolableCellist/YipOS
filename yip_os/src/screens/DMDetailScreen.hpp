#pragma once

#include "ListScreen.hpp"
#include "net/DMClient.hpp"
#include <string>
#include <vector>

namespace YipOS {

class DMDetailScreen : public ListScreen {
public:
    DMDetailScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

protected:
    int ItemCount() const override { return static_cast<int>(messages_.size()); }
    void RenderRow(int i, bool selected) override;
    void WriteSelectionMark(int i, bool selected) override;
    void RenderEmpty() override;

private:
    void RefreshMessages();
    static std::string FormatTimestamp(int64_t date);

    std::string session_id_;
    std::string peer_name_;
    std::vector<DMMessage> messages_;
};

} // namespace YipOS
