#include "INTRPScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "audio/AudioCapture.hpp"
#include "audio/WhisperWorker.hpp"
#ifdef YIPOS_HAS_TRANSLATION
#include "translate/TranslationWorker.hpp"
#endif
#include "core/Config.hpp"
#include "core/Logger.hpp"

namespace YipOS {

using namespace Glyphs;

INTRPScreen::INTRPScreen(PDAController& pda) : Screen(pda) {
    name = "INTRP";
    macro_index = 35;
    update_interval = 0.25f;
    skip_clock = true;

    StartINTRP();
}

INTRPScreen::~INTRPScreen() {
    if (started_by_screen_) {
        StopINTRP();
    }
}

void INTRPScreen::StartINTRP() {
    Logger::Info("INTRP: StartINTRP() called");
    auto& config = pda_.GetConfig();

    // --- Mic whisper (your speech) ---
    auto* whisper_mic = pda_.GetWhisperWorker();
    auto* audio_mic = pda_.GetAudioCapture();

    // --- Loopback whisper (their speech) ---
    auto* whisper_loop = pda_.GetWhisperWorkerLoopback();
    auto* audio_loop = pda_.GetAudioCaptureLoopback();

    if (!whisper_mic || !audio_mic || !whisper_loop || !audio_loop) {
        Logger::Warning("INTRP: Missing audio/whisper instances");
        return;
    }

    // Already running? Don't restart
    if (whisper_mic->IsRunning() && whisper_loop->IsRunning()) {
        Logger::Info("INTRP: Already running, skipping start");
        return;
    }

    // Load model for mic whisper
    std::string saved_model = config.GetState("intrp.model",
                                              config.GetState("cc.model", "tiny.en"));
    if (!whisper_mic->IsModelLoaded()) {
        std::string path = WhisperWorker::DefaultModelPath(saved_model);
        if (!whisper_mic->LoadModel(path)) {
            path = WhisperWorker::DefaultModelPath("tiny.en");
            whisper_mic->LoadModel(path);
        }
    }

    // Load model for loopback whisper (same model)
    if (!whisper_loop->IsModelLoaded()) {
        std::string path = WhisperWorker::DefaultModelPath(saved_model);
        if (!whisper_loop->LoadModel(path)) {
            path = WhisperWorker::DefaultModelPath("tiny.en");
            whisper_loop->LoadModel(path);
        }
    }

    if (!whisper_mic->IsModelLoaded() || !whisper_loop->IsModelLoaded()) {
        Logger::Warning("INTRP: No model available for auto-start");
        return;
    }

    // Configure languages
    std::string my_lang = config.GetState("intrp.my_lang", "en");
    std::string their_lang = config.GetState("intrp.their_lang", "es");
    whisper_mic->SetLanguage(my_lang);
    whisper_loop->SetLanguage(their_lang);

    // Restore saved devices
    std::string mic_device = config.GetState("intrp.mic_device");
    if (!mic_device.empty()) audio_mic->SetDevice(mic_device);

    std::string loop_device = config.GetState("intrp.loop_device");
    if (!loop_device.empty()) audio_loop->SetDevice(loop_device);

    // Start both pipelines
    audio_mic->Start();
    whisper_mic->Start(audio_mic->GetBuffer());

    audio_loop->Start();
    whisper_loop->Start(audio_loop->GetBuffer());

    started_by_screen_ = true;

    // Start translation worker if NLLB model is available
#ifdef YIPOS_HAS_TRANSLATION
    auto* translator = pda_.GetTranslationWorker();
    if (translator && !translator->IsModelLoaded()) {
        std::string nllb_path = TranslationWorker::DefaultModelPath();
        if (TranslationWorker::ModelExists(nllb_path)) {
            if (translator->LoadModel(nllb_path)) {
                Logger::Info("INTRP: NLLB model loaded from " + nllb_path);
            } else {
                Logger::Warning("INTRP: NLLB model failed to load from " + nllb_path);
            }
        } else {
            Logger::Info("INTRP: NLLB model not found at " + nllb_path);
        }
    }
    if (translator && translator->IsModelLoaded() && !translator->IsRunning()) {
        translator->Start();
        Logger::Info("INTRP: Translation worker started");
    }
#else
    Logger::Info("INTRP: Built without translation support");
#endif

    // Save settings
    config.SetState("intrp.model", whisper_mic->GetModelName());
    std::string mic_id = audio_mic->GetCurrentDeviceId();
    if (!mic_id.empty()) config.SetState("intrp.mic_device", mic_id);
    std::string loop_id = audio_loop->GetCurrentDeviceId();
    if (!loop_id.empty()) config.SetState("intrp.loop_device", loop_id);

    Logger::Info("INTRP: Started (my=" + my_lang + " their=" + their_lang + ")");
}

void INTRPScreen::StopINTRP() {
    auto* whisper_mic = pda_.GetWhisperWorker();
    auto* audio_mic = pda_.GetAudioCapture();
    auto* whisper_loop = pda_.GetWhisperWorkerLoopback();
    auto* audio_loop = pda_.GetAudioCaptureLoopback();

    if (whisper_mic && whisper_mic->IsRunning()) whisper_mic->Stop();
    if (audio_mic && audio_mic->IsRunning()) audio_mic->Stop();
    if (whisper_loop && whisper_loop->IsRunning()) whisper_loop->Stop();
    if (audio_loop && audio_loop->IsRunning()) audio_loop->Stop();
    started_by_screen_ = false;
    Logger::Info("INTRP: Stopped");
}

bool INTRPScreen::FilterText(const std::string& text) const {
    if (text.empty()) return true;
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

void INTRPScreen::WordWrap(const std::string& text, std::vector<std::string>& output) {
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

void INTRPScreen::WriteInverted(int col, int row, const std::string& text) {
    for (int i = 0; i < static_cast<int>(text.size()); i++) {
        int ch = static_cast<int>(text[i]) + INVERT_OFFSET;
        display_.WriteChar(col + i, row, ch);
    }
}

void INTRPScreen::Render() {
    RenderFrame("INTRP");

    auto& config = pda_.GetConfig();
    std::string my_lang = config.GetState("intrp.my_lang", "en");
    std::string their_lang = config.GetState("intrp.their_lang", "es");

    // Language indicator on title row (right side)
    // e.g. "EN>ES"
    std::string lang_indicator;
    for (char c : my_lang) lang_indicator += static_cast<char>(toupper(c));
    lang_indicator += ">";
    for (char c : their_lang) lang_indicator += static_cast<char>(toupper(c));
    int ind_col = COLS - 1 - static_cast<int>(lang_indicator.size()) - 1;
    display_.WriteText(ind_col, 0, lang_indicator);

    // Horizontal separator at row 4
    display_.WriteGlyph(0, 4, G_L_TEE);
    for (int c = 1; c < COLS - 1; c++) {
        display_.WriteGlyph(c, 4, G_HLINE);
    }
    display_.WriteGlyph(COLS - 1, 4, G_R_TEE);

    // CONF button (touch 53 area)
    WriteInverted(COLS - 1 - 4, 6, "CONF");

    // Status
    auto* whisper_mic = pda_.GetWhisperWorker();
    auto* whisper_loop = pda_.GetWhisperWorkerLoopback();
    if (!whisper_mic || !whisper_mic->IsModelLoaded() ||
        !whisper_loop || !whisper_loop->IsModelLoaded()) {
        display_.WriteText(2, 2, "No model loaded");
        display_.WriteText(2, 5, "Place ggml model in");
        display_.WriteText(2, 6, "config/models/");
    } else if (!whisper_mic->IsRunning()) {
        display_.WriteText(2, 2, "Starting...");
    }

    // Bottom frame line
    display_.WriteGlyph(0, 7, G_BL_CORNER);
    for (int c = 1; c < COLS - 1; c++) {
        display_.WriteGlyph(c, 7, G_HLINE);
    }
    display_.WriteGlyph(COLS - 1, 7, G_BR_CORNER);
}

void INTRPScreen::RenderDynamic() {
    // No-op: text is written incrementally via Update()
}

void INTRPScreen::Update() {
    auto& config = pda_.GetConfig();
    std::string my_lang = config.GetState("intrp.my_lang", "en");
    std::string their_lang = config.GetState("intrp.their_lang", "es");
    auto* translator = pda_.GetTranslationWorker();
    bool can_translate = translator && translator->IsRunning();

    // --- Submit loopback whisper (their speech) for translation ---
    auto* whisper_loop = pda_.GetWhisperWorkerLoopback();
    if (whisper_loop) {
        while (whisper_loop->HasCommitted()) {
            std::string text = whisper_loop->PopCommitted();
            if (FilterText(text)) continue;
            if (can_translate && their_lang != my_lang) {
                Logger::Debug("INTRP: Loop -> translate (" + their_lang + ">" + my_lang + "): " + text.substr(0, 60));
                translator->Translate(text, their_lang, my_lang, 0);
            } else {
                Logger::Debug("INTRP: Loopback raw (can_translate=" +
                              std::string(can_translate ? "true" : "false") +
                              " langs=" + their_lang + "/" + my_lang + "): " + text.substr(0, 60));
                WordWrap(text, pending_top_);
                while (pending_top_.size() > MAX_PENDING) {
                    pending_top_.erase(pending_top_.begin());
                }
            }
        }
    }

    // --- Submit mic whisper (your speech) for translation ---
    auto* whisper_mic = pda_.GetWhisperWorker();
    if (whisper_mic) {
        while (whisper_mic->HasCommitted()) {
            std::string text = whisper_mic->PopCommitted();
            if (FilterText(text)) continue;
            if (can_translate && my_lang != their_lang) {
                Logger::Debug("INTRP: Mic -> translate (" + my_lang + ">" + their_lang + "): " + text.substr(0, 60));
                translator->Translate(text, my_lang, their_lang, 1);
            } else {
                Logger::Debug("INTRP: Mic raw (can_translate=" +
                              std::string(can_translate ? "true" : "false") +
                              " langs=" + my_lang + "/" + their_lang + "): " + text.substr(0, 60));
                WordWrap(text, pending_bot_);
                while (pending_bot_.size() > MAX_PENDING) {
                    pending_bot_.erase(pending_bot_.begin());
                }
            }
        }
    }

    // --- Pull translated results ---
    if (can_translate) {
        // Channel 0: their speech translated to my language → top half
        while (translator->HasResult(0)) {
            std::string translated = translator->PopResult(0);
            Logger::Debug("INTRP: ch0 translated: " + translated.substr(0, 60));
            if (!translated.empty()) {
                WordWrap(translated, pending_top_);
                while (pending_top_.size() > MAX_PENDING) {
                    pending_top_.erase(pending_top_.begin());
                }
            }
        }
        // Channel 1: my speech translated to their language → bottom half
        while (translator->HasResult(1)) {
            std::string translated = translator->PopResult(1);
            Logger::Debug("INTRP: ch1 translated: " + translated.substr(0, 60));
            if (!translated.empty()) {
                WordWrap(translated, pending_bot_);
                while (pending_bot_.size() > MAX_PENDING) {
                    pending_bot_.erase(pending_bot_.begin());
                }
            }
        }
    }

    // --- Write top half (their speech, rows 1-3) ---
    if (!pending_top_.empty()) {
        display_.BeginBuffered();
        int written = 0;
        while (!pending_top_.empty() && written < LINES_PER_TICK) {
            const std::string& line = pending_top_.front();
            int col = LEFT_COL;
            for (int i = 0; i < static_cast<int>(line.size()); i++) {
                char ch = line[i];
                int char_idx = (ch >= 32 && ch <= 126) ? static_cast<int>(ch) : 32;
                display_.WriteChar(col++, top_cursor_, char_idx);
            }
            while (col <= RIGHT_COL) {
                display_.WriteChar(col++, top_cursor_, 32);
            }
            pending_top_.erase(pending_top_.begin());
            top_cursor_++;
            if (top_cursor_ > TOP_LAST_ROW) top_cursor_ = TOP_FIRST_ROW;
            written++;
        }
    }

    // --- Write bottom half (your speech, rows 5-6) ---
    if (!pending_bot_.empty()) {
        display_.BeginBuffered();
        int written = 0;
        while (!pending_bot_.empty() && written < LINES_PER_TICK) {
            const std::string& line = pending_bot_.front();
            int col = LEFT_COL;
            for (int i = 0; i < static_cast<int>(line.size()); i++) {
                char ch = line[i];
                int char_idx = (ch >= 32 && ch <= 126) ? static_cast<int>(ch) : 32;
                display_.WriteChar(col++, bot_cursor_, char_idx);
            }
            // Don't overwrite CONF button on row 6
            int right_limit = (bot_cursor_ == 6) ? RIGHT_COL - 4 : RIGHT_COL;
            while (col <= right_limit) {
                display_.WriteChar(col++, bot_cursor_, 32);
            }
            pending_bot_.erase(pending_bot_.begin());
            bot_cursor_++;
            if (bot_cursor_ > BOT_LAST_ROW) bot_cursor_ = BOT_FIRST_ROW;
            written++;
        }
    }
}

bool INTRPScreen::OnInput(const std::string& key) {
    // CONF button: touch zone 53 or 52
    if (key == "52" || key == "53") {
        pda_.SetPendingNavigate("INTRP_CONF");
        return true;
    }
    return false;
}

} // namespace YipOS
