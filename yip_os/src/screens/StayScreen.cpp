#include "StayScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Logger.hpp"
#include <cstdio>
#include <random>

namespace YipOS {

using namespace Glyphs;

// Static data
const char* StayScreen::BODY_PARTS[PART_ROWS][5] = {
    {"LW", "RW", "HEAD", nullptr, nullptr},
    {"LF", "RF", "PLAY", nullptr, nullptr},
};

// TILE_CENTERS[0]-3, [1]-3, [3]-3 → [1, 9, 25]
const std::array<int, 3> StayScreen::TILE_POSITIONS = {
    Glyphs::TILE_CENTERS[0] - 3,
    Glyphs::TILE_CENTERS[1] - 3,
    Glyphs::TILE_CENTERS[3] - 3,
};

const std::unordered_map<std::string, StayScreen::PartMapping> StayScreen::CONTACT_MAP = {
    {"12", {0, 0}}, // LW
    {"13", {1, 0}}, // LF
    {"22", {0, 1}}, // RW
    {"23", {1, 1}}, // RF
    {"42", {0, 2}}, // HEAD
    {"43", {1, 2}}, // PLAY
};

StayScreen::StayScreen(PDAController& pda) : Screen(pda) {
    name = "STAY";
    macro_index = 2;
    update_interval = 3;
    locked_ = {
        {"LW", false}, {"RW", false}, {"HEAD", false},
        {"LF", false}, {"RF", false}, {"PLAY", false},
    };
}

void StayScreen::Render() {
    RenderFrame("STAYPUTVR");
    auto& d = display_;

    // Row 1: connection status + drift
    if (connected_) {
        d.WriteGlyph(1, 1, G_CHECK);
        d.WriteText(2, 1, "CONNECTED");
    } else {
        d.WriteGlyph(1, 1, G_XMARK);
        d.WriteText(2, 1, "DISCONNECTED");
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "DRIFT:%5.2fm", drift_);
    d.WriteText(24, 1, buf);

    // Row 2: separator
    d.WriteGlyph(0, 2, G_L_TEE);
    for (int c = 1; c < COLS - 1; c++) d.WriteGlyph(c, 2, G_HLINE);
    d.WriteGlyph(COLS - 1, 2, G_R_TEE);

    // Body parts
    RenderPartRow(0, 3);
    RenderPartRow(1, 5);

    RenderStatusBar();
    Logger::Debug("StayPutVR screen rendered");
}

void StayScreen::RenderDynamic() {
    auto& d = display_;
    if (connected_) {
        d.WriteGlyph(1, 1, G_CHECK);
        d.WriteText(2, 1, "CONNECTED");
    } else {
        d.WriteGlyph(1, 1, G_XMARK);
        d.WriteText(2, 1, "DISCONNECTED");
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%5.2fm", drift_);
    d.WriteText(30, 1, buf);

    // Overwrite locked parts (macro shows unlocked/inverted state)
    for (int part_row = 0; part_row < PART_ROWS; part_row++) {
        for (int i = 0; i < PART_ACTIVE_COLS; i++) {
            const char* part = BODY_PARTS[part_row][i];
            if (part && locked_[part]) {
                RenderSinglePart(part, part_row, i, false);
            }
        }
    }
    RenderClock();
    RenderCursor();
    Logger::Debug("StayPutVR screen dynamic rendered");
}

void StayScreen::RenderPartRow(int part_row, int display_row) {
    auto& d = display_;
    for (int i = 0; i < PART_ACTIVE_COLS; i++) {
        const char* part = BODY_PARTS[part_row][i];
        if (!part) continue;
        int pos = TILE_POSITIONS[i];
        bool is_locked = locked_[part];

        // Lock/unlock glyph shows state
        d.WriteGlyph(pos, display_row, is_locked ? G_LOCK : G_UNLOCK);
        // Label always inverted (touchable)
        char label[8];
        std::snprintf(label, sizeof(label), " %-4s ", part);
        d.WriteText(pos + 1, display_row, label, true);

        // State text — normal for FREE, inverted for LOCKED
        const char* state = is_locked ? "LOCKED" : " FREE ";
        char state_buf[9];
        std::snprintf(state_buf, sizeof(state_buf), "%-8.8s", state);
        d.WriteText(pos, display_row + 1, state_buf, is_locked);
    }
}

void StayScreen::RenderSinglePart(const std::string& part, int part_row, int part_col, bool flash) {
    int display_row = (part_row == 0) ? 3 : 5;
    int pos = TILE_POSITIONS[part_col];
    bool is_locked = locked_[part];
    auto& d = display_;

    // Flash: momentarily show label as normal (un-inverted) to indicate press
    if (flash) {
        char label[8];
        std::snprintf(label, sizeof(label), " %-4s ", part.c_str());
        d.WriteText(pos + 1, display_row, label, false);
    }

    // Lock/unlock glyph
    d.WriteGlyph(pos, display_row, is_locked ? G_LOCK : G_UNLOCK);
    // Label always inverted (touchable)
    char label[8];
    std::snprintf(label, sizeof(label), " %-4s ", part.c_str());
    d.WriteText(pos + 1, display_row, label, true);

    // Clear the state row first to remove any leftover inverted chars,
    // then write the new state. WriteText skips spaces that match the
    // buffer, but the buffer doesn't track inversion — so we must
    // force-clear by writing normal space chars explicitly.
    for (int c = 0; c < 8; c++) {
        d.WriteChar(pos + c, display_row + 1, 32);
    }
    const char* state_str = is_locked ? "LOCKED" : " FREE ";
    char state_buf[9];
    std::snprintf(state_buf, sizeof(state_buf), "%-8.8s", state_str);
    d.WriteText(pos, display_row + 1, state_buf, is_locked);
}

bool StayScreen::OnInput(const std::string& key) {
    auto it = CONTACT_MAP.find(key);
    if (it == CONTACT_MAP.end()) return false;

    int ty = it->second.row;
    int tx = it->second.col;
    const char* part = BODY_PARTS[ty][tx];
    if (!part) return false;

    locked_[part] = !locked_[part];
    std::string state = locked_[part] ? "LOCKED" : "FREE";
    Logger::Info("StayPutVR: " + std::string(part) + " -> " + state);
    RenderSinglePart(part, ty, tx, true);
    return true;
}

void StayScreen::Update() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(-0.01f, 0.01f);
    drift_ = std::max(0.0f, drift_ + dist(rng));

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%5.2f", drift_);
    display_.WriteText(29, 1, buf);
}

} // namespace YipOS
