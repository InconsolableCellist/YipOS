#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>

struct whisper_context;

namespace YipOS {

class AudioRingBuffer;

class WhisperWorker {
public:
    WhisperWorker();
    ~WhisperWorker();

    // Model management
    bool LoadModel(const std::string& model_path);
    bool IsModelLoaded() const { return ctx_ != nullptr; }
    std::string GetModelName() const { return model_name_; }

    // Processing thread
    bool Start(AudioRingBuffer& buffer);
    void Stop();
    bool IsRunning() const { return running_; }

    // Output — thread-safe, dual-channel (tentative + committed)
    // Committed text (consumed once, FIFO)
    bool HasCommitted() const;
    std::string PopCommitted();

    // Tentative text (single slot, overwritten each progressive step)
    std::string GetTentative() const;
    uint32_t GetTentativeVersion() const;

    // Backward compat aliases
    bool HasText() const { return HasCommitted(); }
    std::string PopText() { return PopCommitted(); }
    std::string PeekLatest() const;

    // Configuration
    // Set language for transcription. Call before LoadModel to prevent
    // the default "en" override. Use "auto" for language detection.
    void SetLanguage(const std::string& lang) { language_ = lang; language_locked_ = true; }
    std::string GetLanguage() const { return language_; }

    // Sliding window parameters (milliseconds)
    void SetStepMs(int ms) { step_ms_ = std::max(2000, std::min(ms, 5000)); }
    int GetStepMs() const { return step_ms_; }
    void SetLengthMs(int ms) { length_ms_ = std::max(5000, std::min(ms, 15000)); }
    int GetLengthMs() const { return length_ms_; }

    // Returns true if current model is multilingual (no ".en" suffix)
    bool IsMultilingual() const;

    // Returns true if current model supports the translate flag
    // (false for turbo, distil, and english-only models)
    bool SupportsTranslation() const;

    static std::string DefaultModelPath(const std::string& model_name = "tiny.en");
    static std::vector<std::string> ScanAvailableModels();

private:
    void ProcessLoop();

    whisper_context* ctx_ = nullptr;
    std::string model_name_;
    std::string language_ = "en";
    bool language_locked_ = false; // true if SetLanguage() was called explicitly

    AudioRingBuffer* audio_buffer_ = nullptr;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};

    // Committed output queue
    mutable std::mutex text_mutex_;
    std::queue<std::string> committed_queue_;
    std::string latest_text_;

    // Tentative output (single slot, overwritten each progressive step)
    mutable std::mutex tentative_mutex_;
    std::string tentative_text_;
    std::atomic<uint32_t> tentative_version_{0};

    // Sliding window parameters
    int step_ms_ = 3000;    // run inference every 3s of new audio
    int length_ms_ = 10000; // feed whisper 10s of audio context
    static constexpr int KEEP_MS = 200; // audio overlap across commit boundaries

    static constexpr int WHISPER_SAMPLE_RATE = 16000;
    static constexpr float SILENCE_RMS_THRESHOLD = 0.003f;
};

} // namespace YipOS
