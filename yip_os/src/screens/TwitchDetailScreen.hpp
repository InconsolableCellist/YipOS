#pragma once

#include "Screen.hpp"
#include <string>

namespace YipOS {

struct TwitchMessage;

class TwitchDetailScreen : public Screen {
public:
    TwitchDetailScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    bool OnInput(const std::string& key) override;

private:
    void RenderContent();
    static std::string FormatTimestamp(int64_t date);

    const TwitchMessage* msg_ = nullptr;
};

} // namespace YipOS
