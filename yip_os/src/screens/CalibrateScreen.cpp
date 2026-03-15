#include "CalibrateScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Glyphs.hpp"
#include "core/Logger.hpp"
#include <cstdio>

namespace YipOS {

using namespace Glyphs;

CalibrateScreen::CalibrateScreen(PDAController& pda) : Screen(pda) {
    name = "CLBR";
    macro_index = -1; // no macro — pure text
}

void CalibrateScreen::Render() {
    auto& d = display_;

    // No frame — maximize usable area for the ruler.
    // Every row shows a column ruler so contacts on any row
    // can be read. Alternating tens/units on adjacent rows.
    //
    // Row 0: tens   (zone row 1 = display row 1 is one below)
    // Row 1: units  ← touch zone row 1
    // Row 2: tens
    // Row 3: units
    // Row 4: tens   ← touch zone row 2
    // Row 5: units
    // Row 6: tens   ← touch zone row 3
    // Row 7: units

    for (int row = 0; row < ROWS; row++) {
        bool show_tens = (row % 2 == 0);
        for (int c = 0; c < COLS; c++) {
            char ch;
            if (show_tens) {
                ch = '0' + (c / 10);
            } else {
                ch = '0' + (c % 10);
            }
            d.WriteChar(c, row, static_cast<int>(ch));
        }
    }
}

void CalibrateScreen::RenderContent() {}

bool CalibrateScreen::OnInput(const std::string& key) {
    // Just log — do NOT overwrite the ruler grid
    Logger::Info("CLBR touch: " + key);
    return true;
}

} // namespace YipOS
