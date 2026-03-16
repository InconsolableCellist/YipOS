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

    Logger::Info("CC: Model loaded: " + model_name_);
    return true;
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

void WhisperWorker::ProcessLoop() {
    // Keep a sliding window of audio
    std::vector<float> window;
    window.reserve(WHISPER_SAMPLE_RATE * CHUNK_SECONDS);

    while (running_) {
        // Wait for enough audio
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!running_) break;

        // Read available audio
        size_t avail = audio_buffer_->Available();
        if (avail == 0) continue;

        // Read new samples
        std::vector<float> new_samples(avail);
        size_t read = audio_buffer_->Read(new_samples.data(), avail);
        new_samples.resize(read);

        // Append to sliding window
        window.insert(window.end(), new_samples.begin(), new_samples.end());

        // Cap window size
        size_t max_window = WHISPER_SAMPLE_RATE * CHUNK_SECONDS;
        if (window.size() > max_window) {
            window.erase(window.begin(), window.begin() + (window.size() - max_window));
        }

        // Need minimum audio before processing
        if (window.size() < static_cast<size_t>(MIN_SAMPLES)) continue;

        // Run whisper inference
        struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.print_progress = false;
        wparams.print_special = false;
        wparams.print_realtime = false;
        wparams.print_timestamps = false;
        wparams.single_segment = true;
        wparams.no_context = true;
        wparams.language = language_.c_str();
        wparams.n_threads = 4;

        int result = whisper_full(ctx_, wparams, window.data(), static_cast<int>(window.size()));
        if (result != 0) {
            Logger::Warning("CC: Whisper inference failed");
            continue;
        }

        // Collect segments
        int n_segments = whisper_full_n_segments(ctx_);
        std::string combined;
        for (int i = 0; i < n_segments; i++) {
            const char* text = whisper_full_get_segment_text(ctx_, i);
            if (text) {
                std::string seg = text;
                // Trim leading/trailing whitespace
                while (!seg.empty() && seg.front() == ' ') seg.erase(seg.begin());
                while (!seg.empty() && seg.back() == ' ') seg.pop_back();
                if (!seg.empty()) {
                    if (!combined.empty()) combined += " ";
                    combined += seg;
                }
            }
        }

        if (!combined.empty() && combined != latest_text_) {
            std::lock_guard<std::mutex> lock(text_mutex_);
            latest_text_ = combined;
            text_queue_.push(combined);
            // Keep queue manageable
            while (text_queue_.size() > 50)
                text_queue_.pop();
        }

        // Slide window forward
        size_t step = WHISPER_SAMPLE_RATE * STEP_SECONDS;
        if (window.size() > step) {
            window.erase(window.begin(), window.begin() + step);
        }
    }
}

} // namespace YipOS
