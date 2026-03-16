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

    // Output — thread-safe
    bool HasText() const;
    std::string PopText();
    std::string PeekLatest() const;

    // Configuration
    void SetLanguage(const std::string& lang) { language_ = lang; }

    static std::string DefaultModelPath(const std::string& model_name = "tiny.en");

private:
    void ProcessLoop();

    whisper_context* ctx_ = nullptr;
    std::string model_name_;
    std::string language_ = "en";

    AudioRingBuffer* audio_buffer_ = nullptr;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};

    // Output queue
    mutable std::mutex text_mutex_;
    std::queue<std::string> text_queue_;
    std::string latest_text_;

    // Processing buffer (reusable)
    std::vector<float> process_buf_;

    static constexpr int WHISPER_SAMPLE_RATE = 16000;
    static constexpr int CHUNK_SECONDS = 5;       // process 5 seconds at a time
    static constexpr int STEP_SECONDS = 2;        // step forward 2 seconds between chunks
    static constexpr int MIN_SAMPLES = WHISPER_SAMPLE_RATE * 2; // need at least 2 seconds
};

} // namespace YipOS
