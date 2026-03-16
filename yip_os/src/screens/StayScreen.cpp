#include "StayScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "net/OSCManager.hpp"
#include "core/Logger.hpp"
#include <cstdio>

namespace YipOS {

using namespace Glyphs;

const char* StayScreen::DISPLAY_LABELS[PART_ROWS][PART_ACTIVE_COLS] = {
    {"LW",    "RW",    "COLLAR"},
    {"LF",    "RF",    "ALL"},
};

// 0=HMD, 1=ControllerLeft, 2=ControllerRight, 3=FootLeft, 4=FootRight, 5=Hip
// -1 = ALL
const int StayScreen::SPVR_INDEX[PART_ROWS][PART_ACTIVE_COLS] = {
    {1, 2, 0},   // LW=ControllerLeft, RW=ControllerRight, COLLAR=HMD
    {3, 4, -1},  // LF=FootLeft, RF=FootRight, ALL=global
};

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
    {"42", {0, 2}}, // COLLAR
    {"43", {1, 2}}, // ALL
};

StayScreen::StayScreen(PDAController& pda) : Screen(pda) {
    name = "STAY";
    macro_index = 2;
    update_interval = 1;

    for (int r = 0; r < PART_ROWS; r++)
        for (int c = 0; c < PART_ACTIVE_COLS; c++)
            last_rendered_[r][c] = -999;
}

const char* StayScreen::StatusLabel(int status) const {
    switch (status) {
        case 1:  return "FREE ";
        case 2:  return "LCKD ";
        case 3:  return "WARN ";
        case 4:  return "DSB! ";
        case 5:  return "OOB! ";
        default: return "FREE ";
    }
}

int StayScreen::GetStatus(int part_row, int part_col) const {
    int idx = SPVR_INDEX[part_row][part_col];
    if (idx < 0) return AnyLocked() ? 2 : 1;
    return pda_.GetSPVRStatus(idx);
}

bool StayScreen::AnyLocked() const {
    for (int d = 0; d < PDAController::SPVR_DEVICE_COUNT; d++) {
        if (pda_.GetSPVRStatus(d) >= 2) return true;
    }
    return false;
}

void StayScreen::SendLock(int spvr_index, bool lock) {
    auto* osc = pda_.GetOSCManager();
    if (!osc) return;

    const char* device = PDAController::SPVR_DEVICE_NAMES[spvr_index];
    std::string path = std::string(Glyphs::PARAM_PREFIX) + "SPVR_" + device + "_Latch_IsPosed";
    osc->SendBool(path, lock);
    Logger::Info("SPVR: " + std::string(lock ? "Lock" : "Unlock") + " " + device);
}

void StayScreen::SendGlobalLock(bool lock) {
    auto* osc = pda_.GetOSCManager();
    if (!osc) return;

    std::string path = std::string(Glyphs::PARAM_PREFIX) +
                       (lock ? "SPVR_Global_Lock" : "SPVR_Global_Unlock");
    osc->SendBool(path, true);
    Logger::Info(std::string("SPVR: Global ") + (lock ? "Lock" : "Unlock"));
}

void StayScreen::Render() {
    RenderFrame("STAYPUTVR");

    display_.WriteGlyph(0, 2, G_L_TEE);
    for (int c = 1; c < COLS - 1; c++) display_.WriteGlyph(c, 2, G_HLINE);
    display_.WriteGlyph(COLS - 1, 2, G_R_TEE);

    RenderPartRow(0, 3);
    RenderPartRow(1, 5);
    RenderStatusBar();
}

void StayScreen::RenderDynamic() {
    for (int r = 0; r < PART_ROWS; r++)
        for (int c = 0; c < PART_ACTIVE_COLS; c++)
            RenderSinglePart(r, c);
    RenderClock();
    RenderCursor();
}

void StayScreen::RenderPartRow(int part_row, int display_row) {
    for (int i = 0; i < PART_ACTIVE_COLS; i++) {
        const char* label = DISPLAY_LABELS[part_row][i];
        if (!label) continue;

        int pos = TILE_POSITIONS[i];
        int status = GetStatus(part_row, i);
        bool locked = (status >= 2);

        display_.WriteGlyph(pos, display_row, locked ? G_LOCK : G_UNLOCK);
        char lbl[8];
        std::snprintf(lbl, sizeof(lbl), " %-5s", label);
        display_.WriteText(pos + 1, display_row, lbl, true);

        const char* state_str = StatusLabel(status);
        char state_buf[9];
        std::snprintf(state_buf, sizeof(state_buf), "%-8.8s", state_str);
        display_.WriteText(pos, display_row + 1, state_buf, locked);

        last_rendered_[part_row][i] = status;
    }
}

void StayScreen::RenderSinglePart(int part_row, int part_col, bool flash) {
    const char* label = DISPLAY_LABELS[part_row][part_col];
    if (!label) return;

    int display_row = (part_row == 0) ? 3 : 5;
    int pos = TILE_POSITIONS[part_col];
    int status = GetStatus(part_row, part_col);
    bool locked = (status >= 2);

    if (flash) {
        char lbl[8];
        std::snprintf(lbl, sizeof(lbl), " %-5s", label);
        display_.WriteText(pos + 1, display_row, lbl, false);
    }

    display_.WriteGlyph(pos, display_row, locked ? G_LOCK : G_UNLOCK);
    char lbl[8];
    std::snprintf(lbl, sizeof(lbl), " %-5s", label);
    display_.WriteText(pos + 1, display_row, lbl, true);

    for (int c = 0; c < 8; c++) {
        display_.WriteChar(pos + c, display_row + 1, 32);
    }
    const char* state_str = StatusLabel(status);
    char state_buf[9];
    std::snprintf(state_buf, sizeof(state_buf), "%-8.8s", state_str);
    display_.WriteText(pos, display_row + 1, state_buf, locked);

    last_rendered_[part_row][part_col] = status;
}

bool StayScreen::OnInput(const std::string& key) {
    auto it = CONTACT_MAP.find(key);
    if (it == CONTACT_MAP.end()) return false;

    int ty = it->second.row;
    int tx = it->second.col;
    int spvr_idx = SPVR_INDEX[ty][tx];

    display_.CancelBuffered();

    if (spvr_idx < 0) {
        // ALL: any locked → unlock all, else lock all
        bool do_lock = !AnyLocked();
        SendGlobalLock(do_lock);
        // Update all device statuses immediately
        for (int d = 0; d < PDAController::SPVR_DEVICE_COUNT; d++) {
            pda_.SetSPVRStatus(d, do_lock ? 2 : 1);
        }
    } else {
        // Toggle: locked → unlock, unlocked → lock
        bool locked = (pda_.GetSPVRStatus(spvr_idx) >= 2);
        SendLock(spvr_idx, !locked);
        // Update status immediately so display reflects the action
        pda_.SetSPVRStatus(spvr_idx, locked ? 1 : 2);
    }

    RenderSinglePart(ty, tx, true);
    if (spvr_idx < 0) {
        for (int r = 0; r < PART_ROWS; r++)
            for (int c = 0; c < PART_ACTIVE_COLS; c++)
                if (SPVR_INDEX[r][c] >= 0) RenderSinglePart(r, c);
    }
    return true;
}

void StayScreen::Update() {
    display_.BeginBuffered();
    for (int r = 0; r < PART_ROWS; r++) {
        for (int c = 0; c < PART_ACTIVE_COLS; c++) {
            int status = GetStatus(r, c);
            if (status != last_rendered_[r][c]) {
                RenderSinglePart(r, c);
            }
        }
    }
}

} // namespace YipOS
