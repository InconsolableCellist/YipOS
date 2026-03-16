#include "VRCXFriendDetailScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Logger.hpp"
#include <cstdlib>
#include <cstdio>

namespace YipOS {

using namespace Glyphs;

VRCXFriendDetailScreen::VRCXFriendDetailScreen(PDAController& pda) : Screen(pda) {
    name = "FRIEND";
    macro_index = 13;
    feed_ = pda.GetSelectedFeed();
    LoadData();
}

void VRCXFriendDetailScreen::LoadData() {
    if (!feed_ || feed_->user_id.empty()) return;

    auto* vrcx = pda_.GetVRCXData();
    if (!vrcx || !vrcx->IsOpen()) return;

    friend_info_ = vrcx->GetFriendInfo(feed_->user_id);
    latest_status_ = vrcx->GetLatestUserStatus(feed_->user_id);
    online_count_ = vrcx->GetUserOnlineCount(feed_->user_id);
    time_together_ = vrcx->GetTimeTogether(feed_->user_id);

    // Get latest feed entry for last seen info
    auto latest = vrcx->GetLatestUserFeed(feed_->user_id);
    if (!latest.created_at.empty()) {
        last_seen_ = latest.created_at;
        if (last_seen_.size() > 16) last_seen_ = last_seen_.substr(0, 16);
        last_world_ = latest.world_name;
    }
}

void VRCXFriendDetailScreen::Render() {
    RenderFrame("FRIEND");
    RenderContent();
    RenderStatusBar();
}

void VRCXFriendDetailScreen::RenderDynamic() {
    RenderContent();
    RenderClock();
    RenderCursor();
}

void VRCXFriendDetailScreen::WriteInverted(int col, int row, const std::string& text) {
    for (int i = 0; i < static_cast<int>(text.size()); i++) {
        int ch = static_cast<int>(text[i]) + INVERT_OFFSET;
        display_.WriteChar(col + i, row, ch);
    }
}

void VRCXFriendDetailScreen::FlashButton(int col, int row, const std::string& text) {
    for (int i = 0; i < static_cast<int>(text.size()); i++)
        display_.WriteChar(col + i, row, static_cast<int>(text[i]));
    for (int i = 0; i < static_cast<int>(text.size()); i++)
        display_.WriteChar(col + i, row, static_cast<int>(text[i]) + INVERT_OFFSET);
}

void VRCXFriendDetailScreen::RenderContent() {
    auto& d = display_;

    if (!feed_) {
        d.WriteText(2, 3, "No friend selected");
        return;
    }

    int max_w = COLS - 2;

    // Row 1: Display name
    std::string dname = feed_->display_name;
    if (static_cast<int>(dname.size()) > max_w)
        dname = dname.substr(0, max_w);
    d.WriteText(1, 1, dname);

    // Row 2: Trust level + friend #
    if (friend_info_.found) {
        std::string line = friend_info_.trust_level;
        if (friend_info_.friend_number > 0) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "  #%d", friend_info_.friend_number);
            line += buf;
        }
        if (static_cast<int>(line.size()) > max_w)
            line = line.substr(0, max_w);
        d.WriteText(1, 2, line);
    }

    // Row 3: Status (as VRC color) + description
    if (!latest_status_.status.empty()) {
        // Map VRC status strings to their in-game color names
        std::string color;
        if (latest_status_.status == "active") color = "Green";
        else if (latest_status_.status == "join me") color = "Blue";
        else if (latest_status_.status == "ask me") color = "Orange";
        else if (latest_status_.status == "busy") color = "Red";
        else color = latest_status_.status;

        std::string line = color;
        if (!latest_status_.status_description.empty()) {
            line += ": " + latest_status_.status_description;
        }
        if (static_cast<int>(line.size()) > max_w)
            line = line.substr(0, max_w);
        d.WriteText(1, 3, line);
    }

    // Row 4: Time together + times met
    {
        std::string line;
        if (time_together_.join_count > 0) {
            std::string dur = FormatDuration(time_together_.total_seconds);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Together: %s  Met %dx",
                          dur.c_str(), time_together_.join_count);
            line = buf;
        } else {
            line = "Together: 0";
        }
        if (static_cast<int>(line.size()) > max_w)
            line = line.substr(0, max_w);
        d.WriteText(1, 4, line);
    }

    // Row 5: Last seen timestamp + online event count right-justified
    if (!last_seen_.empty()) {
        d.WriteText(1, 5, "Last: " + last_seen_);
    }
    if (online_count_ > 0) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d evts", online_count_);
        std::string s = buf;
        d.WriteText(COLS - 1 - static_cast<int>(s.size()), 5, s);
    }

    // Row 6: Last world (left) + PROFILE button (right, touch 53)
    if (!last_world_.empty()) {
        std::string prof = "PROFILE";
        int prof_len = static_cast<int>(prof.size());
        int wmax = max_w - prof_len - 1;
        std::string wname = last_world_;
        if (static_cast<int>(wname.size()) > wmax)
            wname = wname.substr(0, wmax);
        d.WriteText(1, 6, wname);
    }
    if (!feed_->user_id.empty()) {
        std::string p1 = "PROFILE";
        WriteInverted(COLS - 1 - static_cast<int>(p1.size()), 6, p1);
    }
}

std::string VRCXFriendDetailScreen::FormatDuration(int64_t seconds) {
    if (seconds <= 0) return "<1m";
    if (seconds < 3600) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%ldm", static_cast<long>(seconds / 60));
        return buf;
    }
    int hrs = static_cast<int>(seconds / 3600);
    int mins = static_cast<int>((seconds % 3600) / 60);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%dh %02dm", hrs, mins);
    return buf;
}

void VRCXFriendDetailScreen::OpenProfile(const std::string& user_id) {
    if (user_id.empty()) return;
    std::string url = "https://vrchat.com/home/user/" + user_id;
    Logger::Info("Opening profile: " + url);
#ifdef _WIN32
    std::string cmd = "start \"\" \"" + url + "\"";
#else
    std::string cmd = "xdg-open '" + url + "' &";
#endif
    std::system(cmd.c_str());
}

bool VRCXFriendDetailScreen::OnInput(const std::string& key) {
    if (!feed_) return false;

    // Touch 53 → PROFILE button
    if (key == "53" && !feed_->user_id.empty()) {
        display_.CancelBuffered();
        display_.BeginBuffered();
        std::string p1 = "PROFILE";
        FlashButton(COLS - 1 - static_cast<int>(p1.size()), 6, p1);
        OpenProfile(feed_->user_id);
        return true;
    }

    return false;
}

} // namespace YipOS
