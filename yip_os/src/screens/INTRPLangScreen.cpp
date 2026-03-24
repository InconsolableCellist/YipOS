#include "INTRPLangScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"

namespace YipOS {

using namespace Glyphs;

const std::vector<INTRPLangScreen::LangEntry> INTRPLangScreen::LANGUAGES = {
    {"en", "English"},
    {"es", "Espanol"},
    {"fr", "Francais"},
    {"de", "Deutsch"},
    {"it", "Italiano"},
    {"ja", "Japanese"},
    {"pt", "Portugues"},
};

INTRPLangScreen::INTRPLangScreen(PDAController& pda) : ListScreen(pda) {
    name = "INTRP LANG";
    macro_index = -1; // no macro, use frame rendering

    editing_field_ = pda_.GetConfig().GetState("intrp.editing", "their");

    // Pre-select current language
    std::string nvram_key = (editing_field_ == "my") ? "intrp.my_lang" : "intrp.their_lang";
    std::string current = pda_.GetConfig().GetState(nvram_key, "en");
    for (int i = 0; i < static_cast<int>(LANGUAGES.size()); i++) {
        if (LANGUAGES[i].code == current) {
            cursor_ = i;
            break;
        }
    }
}

int INTRPLangScreen::ItemCount() const {
    return static_cast<int>(LANGUAGES.size());
}

void INTRPLangScreen::RenderRow(int i, bool selected) {
    auto& d = display_;
    int row = 1 + (i % ROWS_PER_PAGE);
    int col = SEL_WIDTH + 1;

    std::string text = LANGUAGES[i].label;
    // Pad/truncate to fit
    while (static_cast<int>(text.size()) < COLS - col - 1) text += ' ';
    if (static_cast<int>(text.size()) > COLS - col - 1)
        text = text.substr(0, COLS - col - 1);

    d.WriteText(col, row, text);
}

void INTRPLangScreen::WriteSelectionMark(int i, bool selected) {
    auto& d = display_;
    int row = 1 + (i % ROWS_PER_PAGE);
    if (selected) {
        d.WriteChar(1, row, '>' + INVERT_OFFSET);
        d.WriteChar(2, row, ' ' + INVERT_OFFSET);
        d.WriteChar(3, row, ' ' + INVERT_OFFSET);
    } else {
        d.WriteChar(1, row, ' ');
        d.WriteChar(2, row, ' ');
        d.WriteChar(3, row, ' ');
    }
}

bool INTRPLangScreen::OnSelect(int index) {
    if (index < 0 || index >= static_cast<int>(LANGUAGES.size())) return false;

    const auto& lang = LANGUAGES[index];
    std::string nvram_key = (editing_field_ == "my") ? "intrp.my_lang" : "intrp.their_lang";
    pda_.GetConfig().SetState(nvram_key, lang.code);
    Logger::Info("INTRP: Set " + editing_field_ + " language to " + lang.code);

    pda_.SetPendingNavigate("__POP__");
    return true;
}

} // namespace YipOS
