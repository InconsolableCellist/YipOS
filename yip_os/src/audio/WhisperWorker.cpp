#include "WhisperWorker.hpp"
#include "AudioCapture.hpp"
#include "core/Logger.hpp"
#include "core/PathUtils.hpp"

#include <whisper.h>
#include <ggml-vulkan.h>
#include <filesystem>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <chrono>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
// Isolated function for SEH — __try cannot coexist with C++ object unwinding.
// Returns whisper_context* on success, nullptr on crash.
static DWORD g_seh_exception_code = 0;
static whisper_context* TryInitWhisperSEH(const char* path, whisper_context_params cparams) {
    whisper_context* ctx = nullptr;
    g_seh_exception_code = 0;
    __try {
        ctx = whisper_init_from_file_with_params(path, cparams);
    } __except(g_seh_exception_code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        ctx = nullptr;
    }
    return ctx;
}
#endif

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

    // Log file size for diagnostics (corrupted/truncated models crash whisper)
    {
        auto fsize = std::filesystem::file_size(model_path);
        Logger::Info("CC: Loading model: " + model_path +
                     " (" + std::to_string(fsize / (1024 * 1024)) + " MB)");
    }

    struct whisper_context_params cparams = whisper_context_default_params();

    // Check Vulkan GPU availability before attempting GPU init.
    // If no Vulkan devices are found, skip straight to CPU to avoid crashes
    // on systems without proper Vulkan drivers.
    bool try_gpu = true;
    {
        int vk_devices = ggml_backend_vk_get_device_count();
        if (vk_devices <= 0) {
            Logger::Warning("CC: No Vulkan devices found, using CPU only");
            try_gpu = false;
        } else {
            char desc[256] = {};
            ggml_backend_vk_get_device_description(0, desc, sizeof(desc));
            Logger::Info("CC: Vulkan device: " + std::string(desc));
        }
    }

    if (try_gpu) {
        Logger::Info("CC: Initializing with GPU acceleration...");
        cparams.use_gpu = true;
#ifdef _WIN32
        // Vulkan shader compilation can crash (segfault) on some driver versions.
        // Use SEH to catch the crash and fall back to CPU gracefully.
        ctx_ = TryInitWhisperSEH(model_path.c_str(), cparams);
        if (!ctx_) {
            char hex[32];
            snprintf(hex, sizeof(hex), "0x%08lX", g_seh_exception_code);
            Logger::Warning("CC: GPU init failed (SEH exception " + std::string(hex) + "), falling back to CPU");
        }
#else
        ctx_ = whisper_init_from_file_with_params(model_path.c_str(), cparams);
        if (!ctx_) {
            Logger::Warning("CC: GPU init failed, retrying with CPU only...");
        }
#endif
    }

    if (!ctx_) {
        cparams.use_gpu = false;
        Logger::Info("CC: Initializing with CPU...");
#ifdef _WIN32
        ctx_ = TryInitWhisperSEH(model_path.c_str(), cparams);
#else
        ctx_ = whisper_init_from_file_with_params(model_path.c_str(), cparams);
#endif
    }

    if (!ctx_) {
#ifdef _WIN32
        char hex[32];
        snprintf(hex, sizeof(hex), "0x%08lX", g_seh_exception_code);
        Logger::Warning("CC: Failed to load model (SEH exception " + std::string(hex) + ")");
#else
        Logger::Warning("CC: Failed to load model (init failed)");
#endif
        return false;
    }

    // Extract model name from path
    auto fname = std::filesystem::path(model_path).stem().string();
    if (fname.substr(0, 5) == "ggml-") fname = fname.substr(5);
    model_name_ = fname;

    Logger::Info("CC: Model loaded: " + model_name_ +
                 (IsMultilingual() ? " (multilingual)" : " (english-only)") +
                 (SupportsTranslation() ? " (translate OK)" : " (no translate)"));

    // Default to English if no language was explicitly set before loading.
    // INTRP screen sets language before loading to enable multilingual transcription.
    if (!language_locked_) {
        language_ = "en";
    }

    return true;
}

bool WhisperWorker::IsMultilingual() const {
    // English-only models have ".en" in the name (e.g. "tiny.en", "base.en")
    return model_name_.find(".en") == std::string::npos && !model_name_.empty();
}

bool WhisperWorker::SupportsTranslation() const {
    if (model_name_.empty()) return false;
    // English-only models don't support translate
    if (!IsMultilingual()) return false;
    // Turbo and distil models don't support translate
    if (model_name_.find("turbo") != std::string::npos) return false;
    if (model_name_.find("distil") != std::string::npos) return false;
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

bool WhisperWorker::HasCommitted() const {
    std::lock_guard<std::mutex> lock(text_mutex_);
    return !committed_queue_.empty();
}

std::string WhisperWorker::PopCommitted() {
    std::lock_guard<std::mutex> lock(text_mutex_);
    if (committed_queue_.empty()) return "";
    std::string t = std::move(committed_queue_.front());
    committed_queue_.pop();
    return t;
}

std::string WhisperWorker::GetTentative() const {
    std::lock_guard<std::mutex> lock(tentative_mutex_);
    return tentative_text_;
}

uint32_t WhisperWorker::GetTentativeVersion() const {
    return tentative_version_.load();
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

// Collect all segment text from whisper result
static std::string CollectSegments(whisper_context* ctx) {
    int n = whisper_full_n_segments(ctx);
    std::string combined;
    for (int i = 0; i < n; i++) {
        const char* text = whisper_full_get_segment_text(ctx, i);
        if (!text) continue;
        std::string seg = text;
        while (!seg.empty() && seg.front() == ' ') seg.erase(seg.begin());
        while (!seg.empty() && seg.back() == ' ') seg.pop_back();
        if (!seg.empty() && !IsJunkText(seg)) {
            if (!combined.empty()) combined += " ";
            combined += seg;
        }
    }
    return combined;
}

// Extract prompt tokens from last N segments for context carry-forward
static std::vector<whisper_token> ExtractPromptTokens(whisper_context* ctx, int max_tokens) {
    std::vector<whisper_token> tokens;
    int n_seg = whisper_full_n_segments(ctx);
    for (int s = 0; s < n_seg; s++) {
        int n_tok = whisper_full_n_tokens(ctx, s);
        for (int t = 0; t < n_tok; t++) {
            tokens.push_back(whisper_full_get_token_id(ctx, s, t));
        }
    }
    if (static_cast<int>(tokens.size()) > max_tokens) {
        tokens.erase(tokens.begin(), tokens.end() - max_tokens);
    }
    return tokens;
}

// Detect and strip repetition hallucinations.
// Whisper often fills max_tokens by repeating the same phrase.
// Strategy: find the shortest repeating unit and return just one occurrence.
// Returns the de-duplicated text, or the original if no repetition found.
static std::string StripRepetition(const std::string& text) {
    if (text.size() < 10) return text;

    // Try phrase lengths from ~5 words down to 3 words
    // Look for a phrase that repeats 2+ times (with possible trailing truncation)
    // We search for repeated substrings by trying different split points
    for (size_t phrase_len = text.size() / 2; phrase_len >= 8; phrase_len--) {
        std::string candidate = text.substr(0, phrase_len);
        // Trim trailing space from candidate
        while (!candidate.empty() && candidate.back() == ' ') candidate.pop_back();
        if (candidate.size() < 8) continue;

        // Count how many times this phrase appears at the start of successive chunks
        int count = 0;
        size_t pos = 0;
        while (pos + candidate.size() <= text.size()) {
            if (text.compare(pos, candidate.size(), candidate) == 0) {
                count++;
                pos += candidate.size();
                // skip spaces between repetitions
                while (pos < text.size() && text[pos] == ' ') pos++;
            } else {
                break;
            }
        }

        if (count >= 3) {
            // Found a phrase repeated 3+ times — return just one copy
            return candidate;
        }
    }

    return text;
}

// Compute RMS energy of audio buffer
static float ComputeRMS(const float* data, int count) {
    if (count <= 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += static_cast<double>(data[i]) * data[i];
    }
    return static_cast<float>(std::sqrt(sum / count));
}

void WhisperWorker::ProcessLoop() {
    // Sliding window with progressive/commit cycle, matching stream.cpp pattern.
    // Single running buffer (pcmf32) grows as audio arrives. A separate counter
    // (n_samples_new) tracks how much audio has arrived since the last inference.
    // Inference fires only when n_samples_new >= n_samples_step.
    // On commit, the buffer is trimmed to keep_ms_ overlap.

    const int n_samples_step = (step_ms_ * WHISPER_SAMPLE_RATE) / 1000;
    const int n_samples_len = (length_ms_ * WHISPER_SAMPLE_RATE) / 1000;
    const int n_samples_keep = (KEEP_MS * WHISPER_SAMPLE_RATE) / 1000;
    const int n_new_line = std::max(1, length_ms_ / step_ms_ - 1);

    std::vector<float> pcmf32;     // running audio buffer (the sliding window)

    int n_iter = 0;          // iteration counter within commit cycle
    int n_samples_new = 0;   // new samples since last inference
    int inference_count = 0; // total inferences run (for debug)
    std::string last_committed_text; // for overlap stripping

    Logger::Info("CC: sliding window step=" + std::to_string(step_ms_) +
                 "ms len=" + std::to_string(length_ms_) +
                 "ms n_samples_step=" + std::to_string(n_samples_step) +
                 " n_samples_len=" + std::to_string(n_samples_len) +
                 " n_samples_keep=" + std::to_string(n_samples_keep) +
                 " commit_every=" + std::to_string(n_new_line));

    auto loop_start = std::chrono::steady_clock::now();

    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!running_) break;

        // Read available audio into running buffer
        size_t avail = audio_buffer_->Available();
        if (avail == 0) continue;

        std::vector<float> new_samples(avail);
        size_t read = audio_buffer_->Read(new_samples.data(), avail);
        new_samples.resize(read);
        pcmf32.insert(pcmf32.end(), new_samples.begin(), new_samples.end());
        n_samples_new += static_cast<int>(read);

        // Wait until enough NEW audio has arrived for a step
        if (n_samples_new < n_samples_step) continue;

        // Snapshot state BEFORE trimming for debug
        int buf_before_trim = static_cast<int>(pcmf32.size());

        // Cap running buffer to length_ms_ (trim oldest samples)
        int trimmed = 0;
        if (static_cast<int>(pcmf32.size()) > n_samples_len) {
            trimmed = static_cast<int>(pcmf32.size()) - n_samples_len;
            pcmf32.erase(pcmf32.begin(), pcmf32.begin() + trimmed);
        }

        int buf_samples = static_cast<int>(pcmf32.size());
        float buf_duration_ms = (buf_samples * 1000.0f) / WHISPER_SAMPLE_RATE;

        // Skip silence — avoids whisper hallucinations on quiet audio
        float rms = ComputeRMS(pcmf32.data(), buf_samples);
        if (rms < SILENCE_RMS_THRESHOLD) {
            Logger::Debug("CC: silence rms=" + std::to_string(rms) +
                         " buf=" + std::to_string(buf_duration_ms) + "ms, skipping");
            n_samples_new = 0;
            continue;
        }

        auto t0 = std::chrono::steady_clock::now();
        float elapsed_since_start = std::chrono::duration<float>(t0 - loop_start).count();

        Logger::Info("CC: [inf#" + std::to_string(inference_count) +
                     " iter=" + std::to_string(n_iter) + "/" + std::to_string(n_new_line) +
                     " t=" + std::to_string(elapsed_since_start).substr(0, 6) + "s]" +
                     " new=" + std::to_string(n_samples_new) +
                     "(" + std::to_string((n_samples_new * 1000) / WHISPER_SAMPLE_RATE) + "ms)" +
                     " buf=" + std::to_string(buf_samples) +
                     "(" + std::to_string(static_cast<int>(buf_duration_ms)) + "ms)" +
                     " trimmed=" + std::to_string(trimmed) +
                     " rms=" + std::to_string(rms).substr(0, 6));

        // Run inference on the full sliding window.
        // no_context=true: the sliding window audio overlap provides continuity.
        // Prompt token carry-forward causes hallucination loops with sliding windows.
        struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.print_progress = false;
        wparams.print_special = false;
        wparams.print_realtime = false;
        wparams.print_timestamps = false;
        wparams.single_segment = true;
        wparams.max_tokens = 32;
        wparams.no_timestamps = true;
        wparams.no_context = true;
        wparams.language = language_.c_str();
        wparams.translate = SupportsTranslation();
        wparams.n_threads = 4;

        int result = whisper_full(ctx_, wparams,
                                  pcmf32.data(), static_cast<int>(pcmf32.size()));

        auto t1 = std::chrono::steady_clock::now();
        float infer_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();

        n_samples_new = 0; // reset — we've consumed this step's worth
        inference_count++;

        if (result != 0) {
            Logger::Warning("CC: Whisper inference failed (took " +
                           std::to_string(static_cast<int>(infer_ms)) + "ms)");
            continue;
        }

        // Log raw segments before CollectSegments filtering
        int n_seg = whisper_full_n_segments(ctx_);
        for (int s = 0; s < n_seg; s++) {
            const char* seg_text = whisper_full_get_segment_text(ctx_, s);
            int n_tok = whisper_full_n_tokens(ctx_, s);
            Logger::Info("CC:   seg[" + std::to_string(s) + "] tokens=" +
                        std::to_string(n_tok) + " \"" +
                        (seg_text ? seg_text : "(null)") + "\"");
        }

        std::string text_raw = CollectSegments(ctx_);
        bool junk = IsJunkText(text_raw);

        // Strip repetition hallucinations (whisper filling max_tokens with loops)
        std::string text = junk ? text_raw : StripRepetition(text_raw);
        bool was_stripped = (text != text_raw);

        // Strip overlap with previous commit. The sliding window re-processes
        // old audio, so the output often starts with the previous commit's text.
        // Find and remove the overlapping prefix.
        std::string overlap_info;
        if (!text.empty() && !junk && !last_committed_text.empty()) {
            // Check if text starts with the last committed text (exact prefix)
            if (text.size() > last_committed_text.size() &&
                text.compare(0, last_committed_text.size(), last_committed_text) == 0) {
                // Strip the prefix — the new content is after it
                size_t start = last_committed_text.size();
                while (start < text.size() && text[start] == ' ') start++;
                overlap_info = "prefix_stripped=" + std::to_string(last_committed_text.size());
                text = text.substr(start);
            }
            // Check if last commit text contains this text (pure re-transcription)
            else if (last_committed_text.find(text) != std::string::npos) {
                overlap_info = "fully_stale";
                text.clear(); // nothing new
            }
            // Check for partial tail overlap: end of last_committed matches start of text
            else {
                // Try progressively shorter suffixes of last_committed as prefix of text
                size_t max_check = std::min(last_committed_text.size(), text.size());
                size_t best_overlap = 0;
                for (size_t len = max_check; len >= 8; len--) {
                    std::string suffix = last_committed_text.substr(
                        last_committed_text.size() - len);
                    if (text.compare(0, len, suffix) == 0) {
                        best_overlap = len;
                        break;
                    }
                }
                if (best_overlap > 0) {
                    size_t start = best_overlap;
                    while (start < text.size() && text[start] == ' ') start++;
                    overlap_info = "tail_overlap=" + std::to_string(best_overlap);
                    text = text.substr(start);
                }
            }
        }

        Logger::Info("CC:   => collected=\"" + text_raw.substr(0, 80) + "\"" +
                    " junk=" + (junk ? "Y" : "N") +
                    (was_stripped ? " STRIPPED=>\"" + text.substr(0, 40) + "\"" : "") +
                    (!overlap_info.empty() ? " " + overlap_info : "") +
                    " final=\"" + text.substr(0, 60) + "\"" +
                    " infer=" + std::to_string(static_cast<int>(infer_ms)) + "ms" +
                    " segments=" + std::to_string(n_seg));

        n_iter++;
        bool is_commit = (n_iter >= n_new_line);

        if (is_commit) {
            // Commit phase: finalize text, trim audio
            int queue_size = 0;
            if (!text.empty() && !IsJunkText(text)) {
                {
                    std::lock_guard<std::mutex> lock(text_mutex_);
                    latest_text_ = text;
                    committed_queue_.push(text);
                    while (committed_queue_.size() > 50)
                        committed_queue_.pop();
                    queue_size = static_cast<int>(committed_queue_.size());
                }
                last_committed_text = text;
            }

            // Clear tentative on commit
            {
                std::lock_guard<std::mutex> lock(tentative_mutex_);
                tentative_text_.clear();
                tentative_version_++;
            }

            int buf_before_commit_trim = static_cast<int>(pcmf32.size());
            // Trim buffer to keep_ms_ overlap for next cycle
            if (static_cast<int>(pcmf32.size()) > n_samples_keep) {
                pcmf32.erase(pcmf32.begin(),
                             pcmf32.end() - n_samples_keep);
            }

            Logger::Info("CC: === COMMIT === queue=" + std::to_string(queue_size) +
                        " buf_trimmed=" + std::to_string(buf_before_commit_trim) +
                        "->" + std::to_string(pcmf32.size()) +
                        " \"" + text.substr(0, 60) + "\"");
            n_iter = 0;
        } else {
            // Progressive phase: update tentative slot
            if (!text.empty() && !IsJunkText(text)) {
                {
                    std::lock_guard<std::mutex> lock(tentative_mutex_);
                    tentative_text_ = text;
                    tentative_version_++;
                }
            }
            Logger::Info("CC: --- TENTATIVE --- iter=" + std::to_string(n_iter) +
                        "/" + std::to_string(n_new_line) +
                        " \"" + text.substr(0, 60) + "\"");
        }
    }
}

} // namespace YipOS
