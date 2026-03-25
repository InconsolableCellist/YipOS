#pragma once

#include "ListScreen.hpp"
#include <vector>
#include <string>

namespace YipOS {

struct DMSession;

class DMScreen : public ListScreen {
public:
    DMScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

protected:
    int ItemCount() const override { return static_cast<int>(sessions_.size()); }
    void RenderRow(int i, bool selected) override;
    void WriteSelectionMark(int i, bool selected) override;
    void RenderEmpty() override;
    bool OnSelect(int index) override;

private:
    void RefreshSessions();
    static std::string FormatRelativeTime(int64_t date);

    std::vector<const DMSession*> sessions_;
};

} // namespace YipOS
