#include "WhisperWorker.hpp"
#include "AudioCapture.hpp"
#include "core/Logger.hpp"
#include "core/PathUtils.hpp"

#include <whisper.h>
#include <filesystem>
#include <chrono>
#include <algorithm>

namespace YipOS {

WhisperWorker::WhisperWorker() = default;

WhisperWorker::~WhisperWorker() {
    Stop();
    if (ctx_) {
        whisper_free(ctx_);
        ctx_ = nullptr;
    }
}

std::string WhisperWorker::DefaultModelPath(const std::string& model_name) {
    std::string config_dir = GetConfigDir();
    return config_dir + "/models/ggml-" + model_name + ".bin";
}

std::vector<std::string> WhisperWorker::ScanAvailableModels() {
    std::vector<std::string> models;
    namespace fs = std::filesystem;
    std::string models_dir = GetConfigDir() + "/models";
    if (!fs::exists(models_dir)) return models;

    for (auto& entry : fs::directory_iterator(models_dir)) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        // Match ggml-*.bin
        if (fname.size() > 9 && fname.substr(0, 5) == "ggml-" &&
            fname.substr(fname.size() - 4) == ".bin") {
            std::string name = fname.substr(5, fname.size() - 9); // strip ggml- and .bin
            models.push_back(name);
        }
    }
    std::sort(models.begin(), models.end());
    return models;
}

bool WhisperWorker::LoadModel(const std::string& model_path) {
    if (ctx_) {
        whisper_free(ctx_);
        ctx_ = nullptr;
    }

    if (!std::filesystem::exists(model_path)) {
        Logger::Warning("CC: Model not found: " + model_path);
        return false;
    }

    Logger::Info("CC: Loading model: " + model_path);

    struct whisper_context_params cparams = whisper_context_default_params();
    ctx_ = whisper_init_from_file_with_params(model_path.c_str(), cparams);

    if (!ctx_) {
        Logger::Warning("CC: Failed to load model");
        return false;
    }

    // Extract model name from path
    auto fname = std::filesystem::path(model_path).stem().string();
    if (fname.substr(0, 5) == "ggml-") fname = fname.substr(5);
    model_name_ = fname;

    Logger::Info("CC: Model loaded: " + model_name_ +
                 (IsMultilingual() ? " (multilingual)" : " (english-only)"));

    // Force English output — multilingual models with "auto" may transcribe
    // in the detected language, and turbo/distilled models may ignore the
    // translate flag.  Setting language="en" + translate=true ensures
    // English output regardless of model variant.
    language_ = "en";

    return true;
}

bool WhisperWorker::IsMultilingual() const {
    // English-only models have ".en" in the name (e.g. "tiny.en", "base.en")
    return model_name_.find(".en") == std::string::npos && !model_name_.empty();
}

bool WhisperWorker::Start(AudioRingBuffer& buffer) {
    if (running_) return true;
    if (!ctx_) {
        Logger::Warning("CC: Cannot start — no model loaded");
        return false;
    }

    audio_buffer_ = &buffer;
    running_ = true;
    worker_thread_ = std::thread(&WhisperWorker::ProcessLoop, this);
    Logger::Info("CC: Whisper worker started");
    return true;
}

void WhisperWorker::Stop() {
    running_ = false;
    if (worker_thread_.joinable())
        worker_thread_.join();
    audio_buffer_ = nullptr;
    Logger::Info("CC: Whisper worker stopped");
}

bool WhisperWorker::HasText() const {
    std::lock_guard<std::mutex> lock(text_mutex_);
    return !text_queue_.empty();
}

std::string WhisperWorker::PopText() {
    std::lock_guard<std::mutex> lock(text_mutex_);
    if (text_queue_.empty()) return "";
    std::string t = std::move(text_queue_.front());
    text_queue_.pop();
    return t;
}

std::string WhisperWorker::PeekLatest() const {
    std::lock_guard<std::mutex> lock(text_mutex_);
    return latest_text_;
}

// Check if whisper output is junk we should discard
static bool IsJunkText(const std::string& text) {
    if (text.empty()) return true;

    // Common whisper hallucinations for silence/noise
    static const char* junk_phrases[] = {
        "[BLANK_AUDIO]", "[SILENCE]", "[ Silence ]", "(silence)",
        "[Music]", "[music]", "(music)",
        "[foreign language]", "(foreign language)",
        "you", "You", "Thank you.", "Thanks for watching!",
        "Bye.", "Bye!", "...",
    };
    for (auto* phrase : junk_phrases) {
        if (text == phrase) return true;
    }

    // Filter text with no alpha characters
    bool has_alpha = false;
    for (char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            has_alpha = true;
            break;
        }
    }
    return !has_alpha;
}

void WhisperWorker::ProcessLoop() {
    // Accumulate audio, process full chunks, no overlap.
    // This avoids duplicate/partial results from the sliding window approach.
    std::vector<float> chunk;
    chunk.reserve(WHISPER_SAMPLE_RATE * chunk_seconds_);

    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (!running_) break;

        // Read available audio
        size_t avail = audio_buffer_->Available();
        if (avail == 0) continue;

        std::vector<float> new_samples(avail);
        size_t read = audio_buffer_->Read(new_samples.data(), avail);
        new_samples.resize(read);
        chunk.insert(chunk.end(), new_samples.begin(), new_samples.end());

        // Wait until we have a full chunk before processing
        size_t chunk_samples = WHISPER_SAMPLE_RATE * chunk_seconds_;
        if (chunk.size() < chunk_samples) continue;

        // Process the chunk
        struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.print_progress = false;
        wparams.print_special = false;
        wparams.print_realtime = false;
        wparams.print_timestamps = false;
        wparams.single_segment = false;
        wparams.no_context = true;
        wparams.language = language_.c_str();
        wparams.translate = true; // translate all languages to English
        wparams.n_threads = 4;

        int result = whisper_full(ctx_, wparams, chunk.data(), static_cast<int>(chunk.size()));

        // Clear chunk — each piece of audio is processed exactly once
        chunk.clear();

        if (result != 0) {
            Logger::Warning("CC: Whisper inference failed");
            continue;
        }

        // Collect all segments from this chunk
        int n_segments = whisper_full_n_segments(ctx_);
        std::string combined;
        for (int i = 0; i < n_segments; i++) {
            const char* text = whisper_full_get_segment_text(ctx_, i);
            if (text) {
                std::string seg = text;
                while (!seg.empty() && seg.front() == ' ') seg.erase(seg.begin());
                while (!seg.empty() && seg.back() == ' ') seg.pop_back();
                if (!seg.empty() && !IsJunkText(seg)) {
                    if (!combined.empty()) combined += " ";
                    combined += seg;
                }
            }
        }

        if (!combined.empty()) {
            std::lock_guard<std::mutex> lock(text_mutex_);
            latest_text_ = combined;
            text_queue_.push(combined);
            while (text_queue_.size() > 50)
                text_queue_.pop();
        }
    }
}

} // namespace YipOS
