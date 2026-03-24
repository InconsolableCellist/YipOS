#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <array>
#include <condition_variable>

namespace ctranslate2 { class Translator; }
namespace sentencepiece { class SentencePieceProcessor; }

namespace YipOS {

class TranslationWorker {
public:
    TranslationWorker();
    ~TranslationWorker();

    // Load a CTranslate2-format model directory (must contain model.bin + source.spm)
    bool LoadModel(const std::string& model_dir);
    bool IsModelLoaded() const { return model_loaded_; }

    // Submit text for async translation.
    // channel: 0 = their speech → your language, 1 = your speech → their language
    void Translate(const std::string& text,
                   const std::string& src_lang,
                   const std::string& tgt_lang,
                   int channel);

    // Consume translated results (thread-safe, FIFO per channel)
    bool HasResult(int channel) const;
    std::string PopResult(int channel);

    void Start();
    void Stop();
    bool IsRunning() const { return running_; }

    // NLLB language code mapping (e.g. "en" → "eng_Latn")
    static std::string ToNLLBCode(const std::string& short_code);

    // Device info (CPU or CUDA)
    std::string GetDeviceName() const { return device_name_; }

    // Default model path
    static std::string DefaultModelPath();
    static bool ModelExists(const std::string& dir);

private:
    void ProcessLoop();

    struct Request {
        std::string text;
        std::string src_lang; // NLLB code
        std::string tgt_lang; // NLLB code
        int channel;
    };

    std::unique_ptr<ctranslate2::Translator> translator_;
    std::unique_ptr<sentencepiece::SentencePieceProcessor> tokenizer_;
    std::atomic<bool> model_loaded_{false};
    std::string device_name_{"CPU"};
    std::string model_dir_;  // For CUDA→CPU fallback

    std::thread worker_thread_;
    std::atomic<bool> running_{false};

    // Request queue
    std::queue<Request> requests_;
    mutable std::mutex request_mutex_;
    std::condition_variable request_cv_;

    // Per-channel result queues (0 = theirs, 1 = mine)
    static constexpr int CHANNEL_COUNT = 2;
    mutable std::array<std::mutex, CHANNEL_COUNT> result_mutexes_;
    std::array<std::queue<std::string>, CHANNEL_COUNT> result_queues_;
    static constexpr size_t MAX_RESULTS = 50;

    // Last translated text per channel (for UI preview, non-consuming)
    mutable std::array<std::mutex, CHANNEL_COUNT> latest_mutexes_;
    std::array<std::string, CHANNEL_COUNT> latest_translated_;
public:
    std::string PeekLatestTranslated(int channel) const {
        if (channel < 0 || channel >= CHANNEL_COUNT) return {};
        std::lock_guard<std::mutex> lock(latest_mutexes_[channel]);
        return latest_translated_[channel];
    }
};

} // namespace YipOS
