#pragma once

#include "Screen.hpp"
#include "net/VRCXData.hpp"
#include <string>

namespace YipOS {

class VRCXFriendDetailScreen : public Screen {
public:
    VRCXFriendDetailScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

private:
    void LoadData();
    void RenderContent();
    void WriteInverted(int col, int row, const std::string& text);
    void FlashButton(int col, int row, const std::string& text);
    static std::string FormatDuration(int64_t seconds);
    static void OpenProfile(const std::string& user_id);

    const VRCXFeedEntry* feed_ = nullptr;
    VRCXData::FriendInfo friend_info_;
    VRCXStatusEntry latest_status_;
    VRCXData::TimeTogether time_together_;
    int online_count_ = 0;
    std::string last_seen_;
    std::string last_world_;
};

} // namespace YipOS
