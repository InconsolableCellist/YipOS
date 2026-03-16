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
    void SetChunkSeconds(int seconds) { chunk_seconds_ = std::max(2, std::min(seconds, 10)); }
    int GetChunkSeconds() const { return chunk_seconds_; }

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

    int chunk_seconds_ = 3; // adjustable: 2-10 seconds

    static constexpr int WHISPER_SAMPLE_RATE = 16000;
};

} // namespace YipOS
