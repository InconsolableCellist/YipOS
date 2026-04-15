#include "CCScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "audio/AudioCapture.hpp"
#include "audio/WhisperWorker.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "core/TimeUtil.hpp"
#include "net/OSCManager.hpp"

namespace YipOS {

using namespace Glyphs;

CCScreen::CCScreen(PDAController& pda) : Screen(pda) {
    name = "CC";
    macro_index = 14;
    update_interval = 0.25f; // faster tick for responsive captions
    skip_clock = true; // we own the full display, no clock writes

    // Initialize tentative display buffers to spaces (matches initial inverted fill)
    for (auto& s : tent_displayed_) {
        s = std::string(LINE_WIDTH, ' ');
    }

    // Restore chatbox relay state
    chatbox_relay_ = pda_.GetConfig().GetState("cc.chatbox_relay") == "1";

    // Auto-start CC on screen entry
    StartCC();
}

CCScreen::~CCScreen() {
    // Clear VRChat chatbox if relay was active
    if (chatbox_relay_) {
        auto* osc = pda_.GetOSCManager();
        if (osc && osc->IsRunning()) {
            osc->SendChatbox("", true);
        }
    }
    // Auto-stop if we started it
    if (started_by_screen_) {
        StopCC();
    }
}

void CCScreen::StartCC() {
    Logger::Info("CC: StartCC entry");
    auto* whisper = pda_.GetWhisperWorker();
    auto* audio = pda_.GetAudioCapture();
    if (!whisper || !audio) {
        Logger::Warning("CC: StartCC null whisper/audio");
        return;
    }

    // Already running? Don't restart
    if (whisper->IsRunning()) return;

    // Load saved model from NVRAM
    if (!whisper->IsModelLoaded()) {
        std::string saved_model = pda_.GetConfig().GetState("cc.model", "tiny.en");
        std::string path = WhisperWorker::DefaultModelPath(saved_model);
        if (!whisper->LoadModel(path)) {
            // Try fallback
            path = WhisperWorker::DefaultModelPath("tiny.en");
            whisper->LoadModel(path);
        }
    }

    if (!whisper->IsModelLoaded()) {
        Logger::Warning("CC: No model available for auto-start");
        return;
    }

    // Restore saved step/window (guard std::stoi against garbage config)
    Logger::Info("CC: restoring step/window");
    try {
        std::string saved_step = pda_.GetConfig().GetState("cc.step");
        if (!saved_step.empty()) whisper->SetStepMs(std::stoi(saved_step));
        std::string saved_window = pda_.GetConfig().GetState("cc.window");
        if (!saved_window.empty()) whisper->SetLengthMs(std::stoi(saved_window));
    } catch (const std::exception& e) {
        Logger::Warning(std::string("CC: step/window parse failed: ") + e.what());
    }

    // Restore saved audio device
    std::string saved_device = pda_.GetConfig().GetState("cc.device");
    Logger::Info("CC: StartCC saved_device='" + saved_device + "'");
    if (!saved_device.empty()) {
        audio->SetDevice(saved_device);
        Logger::Info("CC: SetDevice returned");
    }

    Logger::Info("CC: calling audio->Start()");
    audio->Start();
    Logger::Info("CC: audio->Start() returned");
    whisper->Start(audio->GetBuffer());
    Logger::Info("CC: whisper->Start() returned");
    started_by_screen_ = true;

    // Save current settings to NVRAM
    pda_.GetConfig().SetState("cc.model", whisper->GetModelName());
    std::string dev_id = audio->GetCurrentDeviceId();
    if (!dev_id.empty()) {
        pda_.GetConfig().SetState("cc.device", dev_id);
    }

    Logger::Info("CC: Auto-started");
}

void CCScreen::StopCC() {
    auto* whisper = pda_.GetWhisperWorker();
    auto* audio = pda_.GetAudioCapture();
    if (whisper && whisper->IsRunning()) whisper->Stop();
    if (audio && audio->IsRunning()) audio->Stop();
    started_by_screen_ = false;
    Logger::Info("CC: Auto-stopped");
}

bool CCScreen::FilterText(const std::string& text) const {
    if (text.empty()) return true;
    // Whisper junk — these are now also filtered at the source (WhisperWorker),
    // but double-check here in case any slip through
    if (text.find("[BLANK_AUDIO]") != std::string::npos) return true;
    if (text.find("[SILENCE]") != std::string::npos) return true;
    if (text.find("[ Silence ]") != std::string::npos) return true;
    if (text.find("(silence)") != std::string::npos) return true;
    if (text.find("[Music]") != std::string::npos) return true;
    if (text.find("[foreign language]") != std::string::npos) return true;
    bool has_alpha = false;
    for (char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            has_alpha = true;
            break;
        }
    }
    if (!has_alpha) return true;
    return false;
}

void CCScreen::WordWrap(const std::string& text, std::vector<std::string>& output) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t remaining = text.size() - pos;
        if (static_cast<int>(remaining) <= LINE_WIDTH) {
            output.push_back(text.substr(pos));
            break;
        }
        size_t end = pos + LINE_WIDTH;
        size_t break_at = text.rfind(' ', end);
        if (break_at == std::string::npos || break_at <= pos) {
            break_at = end;
        }
        output.push_back(text.substr(pos, break_at - pos));
        pos = break_at;
        if (pos < text.size() && text[pos] == ' ') pos++;
    }
}

bool CCScreen::IsTentativeEnabled() const {
    return display_.GetWriteDelay() <= ULTRA_WRITE_DELAY;
}

void CCScreen::Render() {
    RenderFrame("CC");

    auto* whisper = pda_.GetWhisperWorker();
    if (!whisper || !whisper->IsModelLoaded()) {
        display_.WriteText(2, 2, "No model loaded");
        display_.WriteText(2, 3, "Place ggml-tiny.en.bin");
        display_.WriteText(2, 4, "in config/models/");
    } else if (!whisper->IsRunning()) {
        display_.WriteText(2, 3, "Starting...");
    } else {
        display_.WriteText(2, 3, "Listening...");
    }

    // Fill tentative rows with inverted spaces (ULTRA mode)
    // This gives them an inverted background from the start
    if (IsTentativeEnabled()) {
        for (int r = TENT_FIRST_ROW; r <= TENT_LAST_ROW; r++) {
            for (int c = LEFT_COL; c <= RIGHT_COL; c++) {
                display_.WriteChar(c, r, 32 + INVERT_OFFSET);
            }
        }
    }

    // Chatbox relay indicator (persisted across screen entries)
    if (chatbox_relay_) {
        display_.WriteGlyph(COLS - 1, 1, G_RIGHT_A);
    }

    // CONF button on row 6 (touch 53 area)
    WriteInverted(COLS - 1 - 4, 6, "CONF");

    display_.WriteGlyph(0, 7, G_BL_CORNER);
    for (int c = 1; c < COLS - 1; c++) {
        display_.WriteGlyph(c, 7, G_HLINE);
    }
    display_.WriteGlyph(COLS - 1, 7, G_BR_CORNER);
}

void CCScreen::RenderDynamic() {
    // No-op: text is written incrementally via Update()
}

void CCScreen::Update() {
    auto* whisper = pda_.GetWhisperWorker();
    if (!whisper) return;

    // Sync chatbox relay state with config (ImGui may have changed it)
    bool cfg_relay = pda_.GetConfig().GetState("cc.chatbox_relay") == "1";
    if (cfg_relay != chatbox_relay_) {
        chatbox_relay_ = cfg_relay;
        if (!chatbox_relay_) chatbox_buffer_.clear();
        UpdateRelayIndicator();
    }

    bool tentative_enabled = IsTentativeEnabled();

    // Set committed zone bounds based on tentative mode
    committed_first_row_ = 1;
    committed_last_row_ = tentative_enabled ? 4 : 6;

    // Pull committed text from whisper, word-wrap into lines
    int commits_pulled = 0;
    while (whisper->HasCommitted()) {
        std::string text = whisper->PopCommitted();
        commits_pulled++;
        if (FilterText(text)) {
            Logger::Debug("CC.scr: filtered commit #" + std::to_string(commits_pulled) +
                         " \"" + text.substr(0, 40) + "\"");
            continue;
        }
        size_t before = pending_lines_.size();
        WordWrap(text, pending_lines_);
        Logger::Info("CC.scr: commit #" + std::to_string(commits_pulled) +
                    " -> " + std::to_string(pending_lines_.size() - before) + " lines" +
                    " pending=" + std::to_string(pending_lines_.size()) +
                    " \"" + text.substr(0, 50) + "\"");

        // Accumulate for chatbox relay
        if (chatbox_relay_) {
            if (!chatbox_buffer_.empty() && chatbox_buffer_.back() != ' ')
                chatbox_buffer_ += ' ';
            chatbox_buffer_ += text;
            if (static_cast<int>(chatbox_buffer_.size()) > CHATBOX_MAX_LEN)
                chatbox_buffer_ = chatbox_buffer_.substr(
                    chatbox_buffer_.size() - CHATBOX_MAX_LEN);
        }

        // Drop oldest lines if too many pending (keep newest)
        while (pending_lines_.size() > MAX_PENDING_LINES) {
            pending_lines_.erase(pending_lines_.begin());
        }
    }

    // Periodic chatbox send
    if (chatbox_relay_ && !chatbox_buffer_.empty()) {
        double now = MonotonicNow();
        if (now - last_chatbox_send_ >= CHATBOX_INTERVAL) {
            auto* osc = pda_.GetOSCManager();
            if (osc && osc->IsRunning()) {
                osc->SendChatbox(chatbox_buffer_, true);
                Logger::Debug("CC: chatbox relay sent " +
                              std::to_string(chatbox_buffer_.size()) + " chars");
            }
            chatbox_buffer_.clear();
            last_chatbox_send_ = now;
        }
    }

    // Write committed lines
    if (!pending_lines_.empty()) {
        display_.BeginBuffered();

        int lines_written = 0;
        while (!pending_lines_.empty() && lines_written < LINES_PER_TICK) {
            const std::string& line = pending_lines_.front();

            Logger::Debug("CC.scr: write row=" + std::to_string(committed_cursor_) +
                         " zone=[" + std::to_string(committed_first_row_) + "-" +
                         std::to_string(committed_last_row_) + "]" +
                         " \"" + line.substr(0, 38) + "\"");

            // Write text chars, then pad remainder with spaces to clear old content
            int col = LEFT_COL;
            for (int i = 0; i < static_cast<int>(line.size()); i++) {
                char ch = line[i];
                int char_idx = (ch >= 32 && ch <= 126) ? static_cast<int>(ch) : 32;
                display_.WriteChar(col++, committed_cursor_, char_idx);
            }
            while (col <= RIGHT_COL) {
                display_.WriteChar(col++, committed_cursor_, 32);
            }

            pending_lines_.erase(pending_lines_.begin());
            committed_cursor_++;
            if (committed_cursor_ > committed_last_row_) {
                committed_cursor_ = committed_first_row_;
            }
            lines_written++;
        }

        if (!pending_lines_.empty()) {
            Logger::Debug("CC.scr: " + std::to_string(pending_lines_.size()) +
                         " lines still pending after tick");
        }
    }

    // Tentative zone: rows 5-6, diff-based writes (ULTRA only)
    if (tentative_enabled) {
        uint32_t ver = whisper->GetTentativeVersion();
        if (ver != last_tentative_version_) {
            last_tentative_version_ = ver;

            std::string tent_text = whisper->GetTentative();

            Logger::Debug("CC.scr: tentative v" + std::to_string(ver) +
                         " \"" + tent_text.substr(0, 50) + "\"");

            // Word-wrap tentative text into at most 2 rows
            std::vector<std::string> tent_lines;
            if (!tent_text.empty() && !FilterText(tent_text)) {
                WordWrap(tent_text, tent_lines);
            }

            // Pad to exactly 2 lines (pad with empty if needed)
            while (tent_lines.size() < 2) {
                tent_lines.push_back("");
            }
            // Truncate to 2 lines if more
            if (tent_lines.size() > 2) {
                // Keep last 2 lines (most recent content)
                tent_lines.erase(tent_lines.begin(),
                                 tent_lines.end() - 2);
            }

            display_.BeginBuffered();

            // Diff-based write for each tentative row (inverted text)
            int chars_written = 0;
            for (int r = 0; r < 2; r++) {
                int screen_row = TENT_FIRST_ROW + r;
                const std::string& new_line = tent_lines[r];
                const std::string& old_line = tent_displayed_[r];

                // Pad both to LINE_WIDTH for comparison
                std::string new_padded = new_line;
                while (static_cast<int>(new_padded.size()) < LINE_WIDTH) new_padded += ' ';
                std::string old_padded = old_line;
                while (static_cast<int>(old_padded.size()) < LINE_WIDTH) old_padded += ' ';

                // Only write chars that differ — all inverted for visual distinction
                for (int c = 0; c < LINE_WIDTH; c++) {
                    if (new_padded[c] != old_padded[c]) {
                        char ch = new_padded[c];
                        int char_idx = (ch >= 32 && ch <= 126) ? static_cast<int>(ch) : 32;
                        display_.WriteChar(LEFT_COL + c, screen_row, char_idx + INVERT_OFFSET);
                        chars_written++;
                    }
                }

                tent_displayed_[r] = new_padded;
            }
            Logger::Debug("CC.scr: tentative diff wrote " + std::to_string(chars_written) + " chars");
        }
    }
}

void CCScreen::WriteInverted(int col, int row, const std::string& text) {
    for (int i = 0; i < static_cast<int>(text.size()); i++) {
        int ch = static_cast<int>(text[i]) + INVERT_OFFSET;
        display_.WriteChar(col + i, row, ch);
    }
}

void CCScreen::UpdateRelayIndicator() {
    if (chatbox_relay_) {
        display_.WriteGlyph(COLS - 1, 1, G_RIGHT_A);
    } else {
        display_.WriteGlyph(COLS - 1, 1, G_VLINE);
    }
}

bool CCScreen::OnInput(const std::string& key) {
    if (key == "52" || key == "53") {
        pda_.SetPendingNavigate("CC_CONF");
        return true;
    }
    // SEL: toggle VRChat chatbox relay
    if (key == "TR") {
        chatbox_relay_ = !chatbox_relay_;
        if (!chatbox_relay_) {
            chatbox_buffer_.clear();
            auto* osc = pda_.GetOSCManager();
            if (osc && osc->IsRunning()) {
                osc->SendChatbox("", true);
            }
        }
        pda_.GetConfig().SetState("cc.chatbox_relay", chatbox_relay_ ? "1" : "0");
        display_.BeginBuffered();
        UpdateRelayIndicator();
        Logger::Info(std::string("CC: chatbox relay ") + (chatbox_relay_ ? "ON" : "OFF"));
        return true;
    }
    return false;
}

} // namespace YipOS
