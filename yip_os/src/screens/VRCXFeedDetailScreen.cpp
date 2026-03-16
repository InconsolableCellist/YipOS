#include "VRCXFeedDetailScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "net/VRCXData.hpp"
#include "core/Logger.hpp"
#include <cstdlib>
#include <cstdio>
#include <algorithm>

namespace YipOS {

using namespace Glyphs;

VRCXFeedDetailScreen::VRCXFeedDetailScreen(PDAController& pda) : Screen(pda) {
    name = "FEED DTL";
    macro_index = 12;
    feed_ = pda.GetSelectedFeed();
}

void VRCXFeedDetailScreen::Render() {
    RenderFrame("FEED DTL");
    RenderContent();
    RenderStatusBar();
}

void VRCXFeedDetailScreen::RenderDynamic() {
    RenderContent();
    RenderClock();
    RenderCursor();
}

void VRCXFeedDetailScreen::WriteInverted(int col, int row, const std::string& text) {
    for (int i = 0; i < static_cast<int>(text.size()); i++) {
        int ch = static_cast<int>(text[i]) + INVERT_OFFSET;
        display_.WriteChar(col + i, row, ch);
    }
}

void VRCXFeedDetailScreen::FlashButton(int col, int row, const std::string& text) {
    // Un-invert (flash off)
    for (int i = 0; i < static_cast<int>(text.size()); i++)
        display_.WriteChar(col + i, row, static_cast<int>(text[i]));
    // Re-invert (flash on)
    for (int i = 0; i < static_cast<int>(text.size()); i++)
        display_.WriteChar(col + i, row, static_cast<int>(text[i]) + INVERT_OFFSET);
}

void VRCXFeedDetailScreen::RenderContent() {
    auto& d = display_;

    if (!feed_) {
        d.WriteText(2, 3, "No entry selected");
        return;
    }

    int max_w = COLS - 2;

    // Row 1: Display name
    std::string dname = feed_->display_name;
    if (static_cast<int>(dname.size()) > max_w)
        dname = dname.substr(0, max_w);
    d.WriteText(1, 1, dname);

    // Row 2: Online/Offline + timestamp
    std::string status = (feed_->type == "Online") ? "ONLINE" : "OFFLINE";
    std::string ts = feed_->created_at;
    if (ts.size() > 16) ts = ts.substr(0, 16);
    d.WriteText(1, 2, status + "  " + ts);

    // Row 3: World name
    {
        std::string wname = feed_->world_name.empty() ? "(unknown)" : feed_->world_name;
        if (static_cast<int>(wname.size()) > max_w)
            wname = wname.substr(0, max_w);
        d.WriteText(1, 3, wname);
    }

    // Row 4: Instance type + region + time
    if (!feed_->location.empty()) {
        std::string inst = ParseInstanceType(feed_->location);
        std::string region = ParseRegion(feed_->location);
        std::string line = inst;
        if (!region.empty()) line += "  " + region;
        std::string dur = FormatDuration(feed_->time_seconds);
        if (!dur.empty()) line += "  " + dur;
        if (static_cast<int>(line.size()) > max_w)
            line = line.substr(0, max_w);
        d.WriteText(1, 4, line);
    } else if (feed_->time_seconds > 0) {
        d.WriteText(1, 4, "Time: " + FormatDuration(feed_->time_seconds));
    }

    // Row 5-6: Three buttons
    // Left: WRLD DTL (touch 13)
    if (!feed_->location.empty()) {
        WriteInverted(1, 5, "WRLD DTL");
        WriteInverted(1, 6, "(INSTANCE)");
    }

    // Center: FRIEND (touch 33)
    if (!feed_->user_id.empty()) {
        std::string f1 = "FRIEND";
        std::string f2 = "(DETAILS)";
        int fc1 = 20 - static_cast<int>(f1.size()) / 2;
        int fc2 = 20 - static_cast<int>(f2.size()) / 2;
        WriteInverted(fc1, 5, f1);
        WriteInverted(fc2, 6, f2);
    }

    // Right: PROFILE (touch 53)
    if (!feed_->user_id.empty()) {
        std::string p1 = "PROFILE";
        std::string p2 = "(OPN BRWSR)";
        WriteInverted(COLS - 1 - static_cast<int>(p1.size()), 5, p1);
        WriteInverted(COLS - 1 - static_cast<int>(p2.size()), 6, p2);
    }
}

std::string VRCXFeedDetailScreen::ParseInstanceType(const std::string& location) {
    if (location.find("~private(") != std::string::npos) return "Private";
    if (location.find("~hidden(") != std::string::npos) return "Friends+";
    if (location.find("~friends(") != std::string::npos) return "Friends";
    if (location.find("~group(") != std::string::npos) return "Group";
    return "Public";
}

std::string VRCXFeedDetailScreen::ParseRegion(const std::string& location) {
    auto pos = location.find("~region(");
    if (pos == std::string::npos) return "";
    auto start = pos + 8;
    auto end = location.find(')', start);
    if (end == std::string::npos) return "";
    std::string region = location.substr(start, end - start);
    for (auto& c : region) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return region;
}

std::string VRCXFeedDetailScreen::FormatDuration(int64_t seconds) {
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

void VRCXFeedDetailScreen::OpenProfile(const std::string& user_id) {
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

bool VRCXFeedDetailScreen::OnInput(const std::string& key) {
    if (!feed_) return false;

    // Touch 13 (col 1, row 3) → WRLD DTL button
    if (key == "13" && !feed_->location.empty()) {
        display_.CancelBuffered();
        display_.BeginBuffered();
        FlashButton(1, 5, "WRLD DTL");
        FlashButton(1, 6, "(INSTANCE)");

        // Construct a temporary VRCXWorldEntry from feed data
        static VRCXWorldEntry temp_world;
        temp_world.created_at = feed_->created_at;
        temp_world.world_name = feed_->world_name;
        temp_world.world_id = "";
        temp_world.location = feed_->location;
        temp_world.group_name = feed_->group_name;
        temp_world.time_seconds = feed_->time_seconds;

        auto colon = feed_->location.find(':');
        if (colon != std::string::npos)
            temp_world.world_id = feed_->location.substr(0, colon);

        pda_.SetSelectedWorld(&temp_world);
        pda_.SetPendingNavigate("VRCX_WORLD_DETAIL");
        return true;
    }

    // Touch 33 (col 3, row 3) → FRIEND button
    if (key == "33" && !feed_->user_id.empty()) {
        display_.CancelBuffered();
        display_.BeginBuffered();
        std::string f1 = "FRIEND";
        std::string f2 = "(DETAILS)";
        int fc1 = 20 - static_cast<int>(f1.size()) / 2;
        int fc2 = 20 - static_cast<int>(f2.size()) / 2;
        FlashButton(fc1, 5, f1);
        FlashButton(fc2, 6, f2);

        pda_.SetSelectedFeed(feed_);
        pda_.SetPendingNavigate("VRCX_FRIEND_DETAIL");
        return true;
    }

    // Touch 53 (col 5, row 3) → PROFILE button
    if (key == "53" && !feed_->user_id.empty()) {
        display_.CancelBuffered();
        display_.BeginBuffered();

        std::string p1 = "PROFILE";
        std::string p2 = "(OPN BRWSR)";
        int c1 = COLS - 1 - static_cast<int>(p1.size());
        int c2 = COLS - 1 - static_cast<int>(p2.size());
        FlashButton(c1, 5, p1);
        FlashButton(c2, 6, p2);

        OpenProfile(feed_->user_id);
        return true;
    }

    return false;
}

} // namespace YipOS
