#pragma once

#include "ListScreen.hpp"
#include <string>
#include <vector>

namespace YipOS {

class INTRPLangScreen : public ListScreen {
public:
    INTRPLangScreen(PDAController& pda);

    bool OnSelect(int index) override;

protected:
    int ItemCount() const override;
    void RenderRow(int i, bool selected) override;
    void WriteSelectionMark(int i, bool selected) override;

private:
    struct LangEntry {
        std::string code;    // e.g. "en", "es", "ja"
        std::string label;   // e.g. "English", "Espanol"
    };
    static const std::vector<LangEntry> LANGUAGES;
    std::string editing_field_; // "my" or "their"
};

} // namespace YipOS
