#include "VRCXWorldDetailScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "net/VRCXData.hpp"
#include "core/Logger.hpp"
#include <cstdlib>
#include <cstdio>
#include <algorithm>

namespace YipOS {

using namespace Glyphs;

VRCXWorldDetailScreen::VRCXWorldDetailScreen(PDAController& pda) : Screen(pda) {
    name = "WORLD";
    macro_index = 10;
    world_ = pda.GetSelectedWorld();
}

void VRCXWorldDetailScreen::Render() {
    RenderFrame("WORLD");
    RenderContent();
    RenderStatusBar();
}

void VRCXWorldDetailScreen::RenderDynamic() {
    RenderContent();
    RenderClock();
    RenderCursor();
}

void VRCXWorldDetailScreen::RenderContent() {
    auto& d = display_;

    if (!world_) {
        d.WriteText(2, 3, "No world selected");
        return;
    }

    // Row 1: World name (truncated to fit, 38 chars max)
    std::string wname = world_->world_name;
    if (wname.empty()) wname = "(unknown)";
    int max_name = COLS - 2;
    if (static_cast<int>(wname.size()) > max_name) {
        wname = wname.substr(0, max_name);
    }
    d.WriteText(1, 1, wname);

    // Row 2: Instance type + region
    std::string inst_type = ParseInstanceType(world_->location);
    std::string region = ParseRegion(world_->location);
    std::string line2 = inst_type;
    if (!region.empty()) {
        line2 += "  " + region;
    }
    d.WriteText(1, 2, line2);

    // Row 3: Time spent
    std::string dur;
    if (world_->time_seconds <= 0) {
        dur = "<1m";
    } else if (world_->time_seconds < 3600) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%ldm", static_cast<long>(world_->time_seconds / 60));
        dur = buf;
    } else {
        int hrs = static_cast<int>(world_->time_seconds / 3600);
        int mins = static_cast<int>((world_->time_seconds % 3600) / 60);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%dh %02dm", hrs, mins);
        dur = buf;
    }
    d.WriteText(1, 3, "Time: " + dur);

    // Row 4: Timestamp + group name
    std::string ts = world_->created_at;
    if (ts.size() > 16) ts = ts.substr(0, 16); // "2026-03-15 12:34"
    if (!world_->group_name.empty()) {
        std::string gname = world_->group_name;
        int max_g = COLS - 2 - static_cast<int>(ts.size()) - 1;
        if (static_cast<int>(gname.size()) > max_g) {
            gname = gname.substr(0, max_g);
        }
        d.WriteText(1, 4, ts + " " + gname);
    } else {
        d.WriteText(1, 4, ts);
    }

    // Rows 5-6: REJOIN button (inverted, right-aligned near touch 53)
    auto writeInverted = [&](int col, int row, const std::string& text) {
        for (int i = 0; i < static_cast<int>(text.size()); i++) {
            int ch = static_cast<int>(text[i]) + INVERT_OFFSET;
            d.WriteChar(col + i, row, ch);
        }
    };
    std::string rej1 = "REJOIN";
    std::string rej2 = "(OPN BRWSR)";
    int col1 = COLS - 1 - static_cast<int>(rej1.size());
    int col2 = COLS - 1 - static_cast<int>(rej2.size());
    writeInverted(col1, 5, rej1);
    writeInverted(col2, 6, rej2);
}

std::string VRCXWorldDetailScreen::ParseInstanceType(const std::string& location) {
    // Location format: wrld_xxx:12345~private(usr_xxx)~region(us)
    if (location.find("~private(") != std::string::npos) return "Private";
    if (location.find("~hidden(") != std::string::npos) return "Friends+";
    if (location.find("~friends(") != std::string::npos) return "Friends";
    if (location.find("~group(") != std::string::npos) return "Group";
    return "Public";
}

std::string VRCXWorldDetailScreen::ParseRegion(const std::string& location) {
    auto pos = location.find("~region(");
    if (pos == std::string::npos) return "";
    auto start = pos + 8; // length of "~region("
    auto end = location.find(')', start);
    if (end == std::string::npos) return "";
    std::string region = location.substr(start, end - start);
    // Uppercase the region code
    for (auto& c : region) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return region;
}

void VRCXWorldDetailScreen::LaunchWorld(const std::string& location) {
    if (location.empty()) {
        Logger::Warning("Cannot rejoin: empty location");
        return;
    }

    // Split "wrld_xxx:instancePart" into worldId and instanceId
    auto colon = location.find(':');
    std::string world_id, instance_id;
    if (colon != std::string::npos) {
        world_id = location.substr(0, colon);
        instance_id = location.substr(colon + 1);
    } else {
        world_id = location;
    }

    std::string url = "https://vrchat.com/home/launch?worldId=" + world_id +
                      "&instanceId=" + instance_id;
    Logger::Info("Launching: " + url);

#ifdef _WIN32
    std::string cmd = "start \"\" \"" + url + "\"";
#else
    std::string cmd = "xdg-open '" + url + "' &";
#endif
    std::system(cmd.c_str());
}

bool VRCXWorldDetailScreen::OnInput(const std::string& key) {
    Logger::Debug("WORLD detail OnInput: key='" + key + "'");

    // 53 = touch col 5, row 3 — aligns with REJOIN button at row 6
    if (key != "53") return false;

    Logger::Debug("WORLD detail: REJOIN tapped, world_=" +
                  std::string(world_ ? "valid" : "null"));

    if (!world_) {
        Logger::Warning("WORLD detail: TR pressed but no world selected");
        return true;
    }

    Logger::Debug("WORLD detail: location='" + world_->location + "'");
    Logger::Debug("WORLD detail: world_name='" + world_->world_name + "'");

    if (world_->location.empty()) {
        Logger::Warning("WORLD detail: TR pressed but location is empty");
        return true;
    }

    Logger::Info("WORLD detail: REJOIN " + world_->world_name);

    // Flash: un-invert both rows, then re-invert — all buffered so writes drain visibly
    std::string r1 = "REJOIN";
    std::string r2 = "(OPN BRWSR)";
    int c1 = COLS - 1 - static_cast<int>(r1.size());
    int c2 = COLS - 1 - static_cast<int>(r2.size());

    display_.CancelBuffered();
    display_.BeginBuffered();

    // Un-invert (flash off)
    for (int i = 0; i < static_cast<int>(r1.size()); i++)
        display_.WriteChar(c1 + i, 5, static_cast<int>(r1[i]));
    for (int i = 0; i < static_cast<int>(r2.size()); i++)
        display_.WriteChar(c2 + i, 6, static_cast<int>(r2[i]));
    // Re-invert (flash on)
    for (int i = 0; i < static_cast<int>(r1.size()); i++)
        display_.WriteChar(c1 + i, 5, static_cast<int>(r1[i]) + INVERT_OFFSET);
    for (int i = 0; i < static_cast<int>(r2.size()); i++)
        display_.WriteChar(c2 + i, 6, static_cast<int>(r2[i]) + INVERT_OFFSET);

    LaunchWorld(world_->location);
    return true;
}

} // namespace YipOS
