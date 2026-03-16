#pragma once

#include "Screen.hpp"
#include <string>

namespace YipOS {

struct VRCXFeedEntry;

class VRCXFeedDetailScreen : public Screen {
public:
    VRCXFeedDetailScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

private:
    void RenderContent();
    void WriteInverted(int col, int row, const std::string& text);
    void FlashButton(int col, int row, const std::string& text);

    static std::string ParseInstanceType(const std::string& location);
    static std::string ParseRegion(const std::string& location);
    static std::string FormatDuration(int64_t seconds);
    static void OpenProfile(const std::string& user_id);

    const VRCXFeedEntry* feed_ = nullptr;
};

} // namespace YipOS
