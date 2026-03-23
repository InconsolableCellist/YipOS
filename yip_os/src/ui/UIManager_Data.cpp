#include "UIManager.hpp"
#include "app/PDAController.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "net/VRCXData.hpp"
#include "net/VRCAvatarData.hpp"
#include "net/OSCManager.hpp"
#include "net/StockClient.hpp"
#include "net/TwitchClient.hpp"

#include <imgui.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

namespace YipOS {

void UIManager::RenderVRCXTab(PDAController& pda, Config& config) {
    ImGui::Text("VRCX Integration");
    ImGui::TextDisabled("Reads world history and feed from VRCX's local SQLite database.");
    ImGui::TextDisabled("Requires VRCX to be installed. Data is read-only.");

    ImGui::Separator();

    if (ImGui::Checkbox("Enable VRCX", &config.vrcx_enabled)) {
        if (!config_path_.empty()) config.SaveToFile(config_path_);
    }

    if (config.vrcx_enabled) {
        if (!vrcx_path_initialized_) {
            std::string initial = config.vrcx_db_path.empty()
                ? VRCXData::DefaultDBPath() : config.vrcx_db_path;
            std::snprintf(vrcx_path_buf_.data(), vrcx_path_buf_.size(), "%s", initial.c_str());
            vrcx_path_initialized_ = true;
        }

        ImGui::InputText("DB Path", vrcx_path_buf_.data(), vrcx_path_buf_.size());
#ifdef _WIN32
        ImGui::TextDisabled("Default: %%APPDATA%%\\VRCX\\VRCX.sqlite3");
#else
        ImGui::TextDisabled("Default: ~/.config/VRCX/VRCX.sqlite3");
#endif

        VRCXData* vrcx = pda.GetVRCXData();
        if (vrcx && vrcx->IsOpen()) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Connected");
            ImGui::SameLine();
            ImGui::Text("(%d worlds)", vrcx->GetWorldCount());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Not connected");
        }

        if (ImGui::Button("Connect")) {
            std::string path(vrcx_path_buf_.data());
            config.vrcx_db_path = path;
            if (vrcx) {
                vrcx->Close();
                if (vrcx->Open(path)) {
                    Logger::Info("VRCX reconnected: " + path);
                } else {
                    Logger::Warning("VRCX connect failed: " + path);
                }
            }
            if (!config_path_.empty()) config.SaveToFile(config_path_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Disconnect")) {
            if (vrcx) vrcx->Close();
        }
    } else {
        vrcx_path_initialized_ = false;
        VRCXData* vrcx = pda.GetVRCXData();
        if (vrcx && vrcx->IsOpen()) vrcx->Close();
    }
}

void UIManager::RenderAvatarTab(PDAController& pda, Config& config) {
    ImGui::Text("Avatar Management");
    ImGui::TextDisabled("Browse and switch VRChat avatars via OSC. Reads avatar data");
    ImGui::TextDisabled("from VRChat's local OSC cache directory.");

    ImGui::Separator();

    if (!avtr_path_initialized_) {
        std::string initial = config.vrc_osc_path;
        if (initial.empty()) initial = YipOS::VRCAvatarData::DefaultOSCPath();
        std::snprintf(avtr_path_buf_.data(), avtr_path_buf_.size(), "%s", initial.c_str());
        avtr_path_initialized_ = true;
    }
    ImGui::InputText("VRC OSC Path", avtr_path_buf_.data(), avtr_path_buf_.size());
#ifdef _WIN32
    ImGui::TextDisabled("Default: %%LOCALAPPDATA%%Low/VRChat/VRChat/OSC/");
#else
    ImGui::TextDisabled("Linux: set to your compatdata .../VRChat/VRChat/OSC/");
#endif

    auto* avtr = pda.GetAvatarData();
    if (avtr) {
        ImGui::Text("Avatars found: %d", static_cast<int>(avtr->GetAvatars().size()));
        if (!avtr->GetCurrentAvatarId().empty()) {
            auto* current = avtr->GetCurrentAvatar();
            if (current) {
                ImGui::Text("Current: %s", current->name.c_str());
            }
        }
    }

    if (ImGui::Button("Rescan Avatars")) {
        std::string path(avtr_path_buf_.data());
        config.vrc_osc_path = path;
        if (avtr) avtr->Scan(path);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Path")) {
        config.vrc_osc_path = avtr_path_buf_.data();
        if (!config_path_.empty()) config.SaveToFile(config_path_);
    }
}

void UIManager::RenderTextTab(PDAController& pda, Config& config, OSCManager& osc) {
    ImGui::Text("Text Display");
    ImGui::TextDisabled("Text appears live on the PDA TEXT screen.");

    ImGui::Separator();

    if (!text_buf_initialized_) {
        std::string saved = config.GetState("text.content");
        std::snprintf(text_buf_.data(), text_buf_.size(), "%s", saved.c_str());
        text_vrc_chatbox_ = config.GetState("text.vrc_chatbox") == "1";
        text_buf_initialized_ = true;
        // Initialize display text from saved
        pda.SetDisplayText(saved);
    }

    if (!text_vrc_chatbox_) {
        ImGui::Text("Message");
        ImGui::InputTextMultiline("##text_content", text_buf_.data(), text_buf_.size(),
                                  ImVec2(-1, 120));

        // Push text to PDAController live on every frame
        pda.SetDisplayText(std::string(text_buf_.data()));

        if (ImGui::Button("Save")) {
            config.SetState("text.content", std::string(text_buf_.data()));
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Persist to config");
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            text_buf_[0] = '\0';
            pda.SetDisplayText("");
        }
    } else {
        // Chatbox mode: show received text read-only
        std::string chatbox = osc.GetChatboxText();
        if (!chatbox.empty()) {
            ImGui::TextWrapped("Chatbox: %s", chatbox.c_str());
        } else {
            ImGui::TextDisabled("Waiting for /chatbox/input on listen port...");
        }
    }

    ImGui::Separator();

    ImGui::Text("VRChat ChatBox");
    ImGui::TextDisabled("Display text from external apps that send /chatbox/input.");
    if (ImGui::Checkbox("Use VRC ChatBox input", &text_vrc_chatbox_)) {
        config.SetState("text.vrc_chatbox", text_vrc_chatbox_ ? "1" : "0");
    }
    ImGui::TextDisabled("When enabled, listens for /chatbox/input on the OSC listen port");
    ImGui::TextDisabled("and displays that text instead of the manual input above.");
}

void UIManager::RenderStocksTab(PDAController& pda, Config& config) {
    ImGui::Text("Stock & Crypto Monitor (STONK)");
    ImGui::TextDisabled("Real-time price graphs on the PDA. Data from Yahoo Finance (no API key).");
    ImGui::TextDisabled("Crypto symbols (BTC, DOGE, etc.) are auto-suffixed with -USD.");

    ImGui::Separator();

    // Enable checkbox
    bool enabled = config.GetState("stonk.enabled", "0") == "1";
    if (ImGui::Checkbox("Enable STONK", &enabled)) {
        config.SetState("stonk.enabled", enabled ? "1" : "0");
    }

    ImGui::Separator();

    // Symbol list
    ImGui::Text("Watched Symbols");
    std::string sym_str = config.GetState("stonk.symbols", "DOGE,BTC,AAPL,NVDA,GME");

    // Parse current symbols
    std::vector<std::string> symbols;
    {
        size_t start = 0;
        while (start < sym_str.size()) {
            size_t end = sym_str.find(',', start);
            if (end == std::string::npos) end = sym_str.size();
            std::string s = sym_str.substr(start, end - start);
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            while (!s.empty() && s.back() == ' ') s.pop_back();
            if (!s.empty()) symbols.push_back(s);
            start = end + 1;
        }
    }

    // Display list with remove buttons
    int remove_idx = -1;
    for (int i = 0; i < static_cast<int>(symbols.size()); i++) {
        ImGui::BulletText("%s", symbols[i].c_str());
        ImGui::SameLine(200);
        std::string btn_id = "Remove##sym_" + std::to_string(i);
        if (ImGui::SmallButton(btn_id.c_str())) {
            remove_idx = i;
        }
    }
    if (remove_idx >= 0) {
        symbols.erase(symbols.begin() + remove_idx);
        std::string new_str;
        for (int i = 0; i < static_cast<int>(symbols.size()); i++) {
            if (i > 0) new_str += ',';
            new_str += symbols[i];
        }
        config.SetState("stonk.symbols", new_str);
    }

    // Add symbol
    ImGui::InputText("##add_sym", stonk_symbol_buf_.data(), stonk_symbol_buf_.size());
    ImGui::SameLine();
    if (ImGui::Button("Add")) {
        std::string new_sym(stonk_symbol_buf_.data());
        // Trim and uppercase
        while (!new_sym.empty() && new_sym.front() == ' ') new_sym.erase(new_sym.begin());
        while (!new_sym.empty() && new_sym.back() == ' ') new_sym.pop_back();
        for (auto& c : new_sym) c = static_cast<char>(toupper(c));
        if (!new_sym.empty()) {
            symbols.push_back(new_sym);
            std::string new_str;
            for (int i = 0; i < static_cast<int>(symbols.size()); i++) {
                if (i > 0) new_str += ',';
                new_str += symbols[i];
            }
            config.SetState("stonk.symbols", new_str);
            stonk_symbol_buf_[0] = '\0';
        }
    }

    ImGui::Separator();

    // Refresh interval
    ImGui::Text("Refresh Interval");
    std::string refresh_str = config.GetState("stonk.refresh", "300");
    int refresh_sec = 300;
    try { refresh_sec = std::stoi(refresh_str); }
    catch (...) {}
    if (ImGui::SliderInt("Seconds##stonk_refresh", &refresh_sec, 60, 900)) {
        config.SetState("stonk.refresh", std::to_string(refresh_sec));
    }
    ImGui::TextDisabled("How often to fetch new price data (60s - 900s).");

    // Manual fetch
    if (ImGui::Button("Fetch Now")) {
        pda.ReloadStockSymbols();
        auto* client = pda.GetStockClient();
        if (client) {
            std::string window = config.GetState("stonk.window", "1MO");
            client->FetchAll(window);
        }
    }

    ImGui::Separator();

    // Show current data if available
    auto* client = pda.GetStockClient();
    if (client && !client->GetQuotes().empty()) {
        ImGui::Text("Cached Data");
        for (auto& q : client->GetQuotes()) {
            float pct = 0;
            if (q.prev_close > 0) pct = ((q.current_price - q.prev_close) / q.prev_close) * 100.0f;
            ImGui::Text("  %s: $%.4f (%+.1f%%)", q.symbol.c_str(), q.current_price, pct);
        }
    }
}

void UIManager::RenderIMGTab(PDAController& pda, Config& config) {
    ImGui::Text("Image Display (IMG)");
    ImGui::TextDisabled("Display images on the PDA via vector quantization (VQ encoding).");
    ImGui::TextDisabled("Drag and drop an image onto this window to send it to the PDA.");

    ImGui::Separator();

    // Images directory
    std::string assets = pda.GetAssetsPath();
    if (assets.empty()) assets = "assets";
    namespace fs = std::filesystem;
    fs::path img_dir = fs::path(assets) / "images";

    ImGui::Text("Images Directory");
    ImGui::TextDisabled("%s", img_dir.string().c_str());

    if (ImGui::Button("Open Folder")) {
#ifdef _WIN32
        // ShellExecuteA to open the images folder in Explorer
        std::string dir_str = img_dir.string();
        // Create directory if it doesn't exist
        fs::create_directories(img_dir);
        ShellExecuteA(nullptr, "explore", dir_str.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Place .jpg/.png/.bmp files here");

    ImGui::Separator();

    // List images in the directory
    ImGui::Text("Available Images");
    if (fs::exists(img_dir) && fs::is_directory(img_dir)) {
        int count = 0;
        for (const auto& entry : fs::directory_iterator(img_dir)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp") {
                std::string fname = entry.path().filename().string();
                ImGui::BulletText("%s", fname.c_str());
                ImGui::SameLine(300);
                std::string btn_id = "Send##img_" + std::to_string(count);
                if (ImGui::SmallButton(btn_id.c_str())) {
                    pda.SetDroppedImagePath(entry.path().string());
                }
                count++;
            }
        }
        if (count == 0) {
            ImGui::TextDisabled("No images found. Add .jpg/.png/.bmp files to the folder above.");
        }
    } else {
        ImGui::TextDisabled("Directory does not exist. Click 'Open Folder' to create it.");
    }

    ImGui::Separator();

    ImGui::Text("Drag & Drop");
    ImGui::TextDisabled("Drag any image file onto this window to display it on the PDA.");
    ImGui::TextDisabled("Supported formats: .jpg, .jpeg, .png, .bmp");
    ImGui::Spacing();
    ImGui::TextDisabled("Images are converted to 1-bit dithered 32x32 blocks via VQ encoding,");
    ImGui::TextDisabled("preserving aspect ratio with letterboxing.");
}

void UIManager::RenderTwitchTab(PDAController& pda, Config& config) {
    ImGui::Text("Twitch Chat Viewer (TWTCH)");
    ImGui::TextDisabled("Display live Twitch chat on the PDA. Read-only, no account needed.");

    ImGui::Separator();

    // Enable checkbox
    bool enabled = config.GetState("twitch.enabled", "0") == "1";
    if (ImGui::Checkbox("Enable Twitch", &enabled)) {
        config.SetState("twitch.enabled", enabled ? "1" : "0");
        auto* client = pda.GetTwitchClient();
        if (client) {
            if (enabled && !client->GetChannel().empty() && !client->IsConnected()) {
                client->Connect();
            } else if (!enabled && client->IsConnected()) {
                client->Disconnect();
            }
        }
    }

    ImGui::Separator();

    // Channel name
    ImGui::Text("Channel");
    ImGui::TextDisabled("The Twitch channel to read chat from (without the #).");
    ImGui::TextDisabled("Example: \"foxipso\" to read chat from twitch.tv/foxipso");

    if (!twitch_channel_initialized_) {
        std::string ch = config.GetState("twitch.channel");
        std::snprintf(twitch_channel_buf_.data(), twitch_channel_buf_.size(), "%s", ch.c_str());
        twitch_channel_initialized_ = true;
    }

    ImGui::InputText("##twitch_channel", twitch_channel_buf_.data(), twitch_channel_buf_.size());
    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
        std::string channel(twitch_channel_buf_.data());
        // Trim whitespace and lowercase
        while (!channel.empty() && channel.front() == ' ') channel.erase(channel.begin());
        while (!channel.empty() && channel.back() == ' ') channel.pop_back();
        for (auto& c : channel) c = static_cast<char>(tolower(c));
        // Strip leading # if user included it
        if (!channel.empty() && channel[0] == '#') channel.erase(channel.begin());

        config.SetState("twitch.channel", channel);
        std::snprintf(twitch_channel_buf_.data(), twitch_channel_buf_.size(), "%s", channel.c_str());

        auto* client = pda.GetTwitchClient();
        if (client) {
            // Disconnect and reconnect with new channel
            if (client->IsConnected()) client->Disconnect();
            client->SetChannel(channel);
            if (enabled && !channel.empty()) {
                client->Connect();
            }
        }
    }

    ImGui::Separator();

    // Connection status
    ImGui::Text("Status");
    auto* client = pda.GetTwitchClient();
    if (client) {
        std::string channel = client->GetChannel();
        if (channel.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "No channel configured");
        } else if (!enabled) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Disabled");
        } else if (client->IsConnected()) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Connected to #%s", channel.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Connecting to #%s...", channel.c_str());
        }

        ImGui::Text("Messages: %d", client->GetMessageCount());

        // Connect/Disconnect buttons
        if (enabled && !channel.empty()) {
            if (client->IsConnected()) {
                if (ImGui::Button("Reconnect")) {
                    client->Disconnect();
                    client->Connect();
                }
            } else {
                if (ImGui::Button("Connect Now")) {
                    client->Connect();
                }
            }
        }
    }

    ImGui::Separator();

    // How it works
    ImGui::Text("How It Works");
    ImGui::TextDisabled("Yip OS connects to Twitch IRC as an anonymous viewer.");
    ImGui::TextDisabled("No Twitch account, API key, or OAuth token is required.");
    ImGui::TextDisabled("Only public chat messages are received (read-only).");
    ImGui::Spacing();
    ImGui::TextDisabled("Navigate to page 2 on the PDA home screen and tap TWTCH");
    ImGui::TextDisabled("to view the chat feed. New messages appear automatically.");
    ImGui::Spacing();
    ImGui::TextDisabled("Controls on the PDA:");
    ImGui::TextDisabled("  Joystick = cycle cursor    SEL (TR) = view message");
    ImGui::TextDisabled("  ML = page up               BL = page down");
}

} // namespace YipOS
