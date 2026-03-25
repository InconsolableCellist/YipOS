#include "TranslationWorker.hpp"
#include "core/Logger.hpp"
#include "core/PathUtils.hpp"
#ifdef YIPOS_HAS_MECAB
#include "translate/MeCabWrapper.hpp"
#endif

#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>
#include <filesystem>
#include <fstream>
#include <cstdio>

namespace YipOS {

// Map short language codes to NLLB-200 language codes
static const struct { const char* short_code; const char* nllb_code; } LANG_MAP[] = {
    {"en", "eng_Latn"},
    {"es", "spa_Latn"},
    {"fr", "fra_Latn"},
    {"de", "deu_Latn"},
    {"it", "ita_Latn"},
    {"ja", "jpn_Jpan"},
    {"pt", "por_Latn"},
};
static constexpr int LANG_MAP_SIZE = sizeof(LANG_MAP) / sizeof(LANG_MAP[0]);

std::string TranslationWorker::ToNLLBCode(const std::string& short_code) {
    for (int i = 0; i < LANG_MAP_SIZE; i++) {
        if (short_code == LANG_MAP[i].short_code)
            return LANG_MAP[i].nllb_code;
    }
    return "eng_Latn"; // fallback
}

TranslationWorker::TranslationWorker() = default;

TranslationWorker::~TranslationWorker() {
    Stop();
}

std::string TranslationWorker::DefaultModelPath() {
    return GetConfigDir() + "/models/nllb";
}

bool TranslationWorker::ModelExists(const std::string& dir) {
    namespace fs = std::filesystem;
    if (!fs::exists(dir + "/model.bin")) return false;
    bool has_spm = fs::exists(dir + "/source.spm") ||
                   fs::exists(dir + "/sentencepiece.model") ||
                   fs::exists(dir + "/sentencepiece.bpe.model");
    bool has_vocab = fs::exists(dir + "/shared_vocabulary.txt") ||
                     fs::exists(dir + "/shared_vocabulary.json");
    return has_spm && has_vocab;
}

// Generate config.json if missing — NLLB models always use the same special tokens
static void EnsureConfigJson(const std::string& dir) {
    std::string path = dir + "/config.json";
    if (std::filesystem::exists(path)) return;
    std::ofstream f(path);
    if (f.is_open()) {
        f << "{\n"
          << "  \"add_source_bos\": false,\n"
          << "  \"add_source_eos\": false,\n"
          << "  \"bos_token\": \"<s>\",\n"
          << "  \"decoder_start_token\": \"</s>\",\n"
          << "  \"eos_token\": \"</s>\",\n"
          << "  \"unk_token\": \"<unk>\"\n"
          << "}\n";
        YipOS::Logger::Info("NLLB: Generated config.json");
    }
}

bool TranslationWorker::LoadModel(const std::string& model_dir) {
    namespace fs = std::filesystem;
    if (!ModelExists(model_dir)) {
        // Log which specific files are missing
        std::string missing;
        if (!fs::exists(model_dir + "/model.bin")) missing += " model.bin";
        if (!fs::exists(model_dir + "/source.spm") &&
            !fs::exists(model_dir + "/sentencepiece.model") &&
            !fs::exists(model_dir + "/sentencepiece.bpe.model"))
            missing += " sentencepiece.bpe.model";
        if (!fs::exists(model_dir + "/shared_vocabulary.txt") &&
            !fs::exists(model_dir + "/shared_vocabulary.json"))
            missing += " shared_vocabulary.txt";
        Logger::Warning("NLLB: Missing files in " + model_dir + ":" + missing);
        return false;
    }

    EnsureConfigJson(model_dir);

    try {
        // Load SentencePiece tokenizer
        tokenizer_ = std::make_unique<sentencepiece::SentencePieceProcessor>();
        std::string spm_path = model_dir + "/source.spm";
        if (!std::filesystem::exists(spm_path))
            spm_path = model_dir + "/sentencepiece.model";
        if (!std::filesystem::exists(spm_path))
            spm_path = model_dir + "/sentencepiece.bpe.model";

        auto status = tokenizer_->Load(spm_path);
        if (!status.ok()) {
            Logger::Error("NLLB: Failed to load tokenizer: " + status.ToString());
            tokenizer_.reset();
            return false;
        }

        model_dir_ = model_dir;

        // Try CUDA first for GPU acceleration, fall back to CPU
        try {
            translator_ = std::make_unique<ctranslate2::Translator>(
                model_dir, ctranslate2::Device::CUDA, ctranslate2::ComputeType::AUTO);
            device_name_ = "CUDA";
            Logger::Info("NLLB: Using CUDA acceleration");
        } catch (...) {
            translator_ = std::make_unique<ctranslate2::Translator>(
                model_dir, ctranslate2::Device::CPU, ctranslate2::ComputeType::AUTO);
            device_name_ = "CPU";
            Logger::Info("NLLB: CUDA unavailable, using CPU");
        }

        model_loaded_ = true;
        Logger::Info("NLLB: Model loaded from " + model_dir + " (" + device_name_ + ")");

#ifdef YIPOS_HAS_MECAB
        mecab_ = std::make_unique<MeCabWrapper>();
        if (mecab_->Init()) {
            Logger::Info("NLLB: MeCab available for Japanese kanji→hiragana");
        } else {
            mecab_.reset();
            Logger::Warning("NLLB: MeCab unavailable — Japanese kanji will display as '?'");
        }
#endif

        return true;
    } catch (const std::exception& e) {
        Logger::Error("NLLB: Failed to load model: " + std::string(e.what()));
        translator_.reset();
        tokenizer_.reset();
        model_loaded_ = false;
        return false;
    }
}

void TranslationWorker::Start() {
    if (running_ || !model_loaded_) return;
    running_ = true;
    worker_thread_ = std::thread(&TranslationWorker::ProcessLoop, this);
    Logger::Info("NLLB: Translation worker started");
}

void TranslationWorker::Stop() {
    if (!running_) return;
    running_ = false;
    request_cv_.notify_all();
    if (worker_thread_.joinable()) worker_thread_.join();
    Logger::Info("NLLB: Translation worker stopped");
}

void TranslationWorker::Translate(const std::string& text,
                                   const std::string& src_lang,
                                   const std::string& tgt_lang,
                                   int channel) {
    if (!model_loaded_ || !running_) {
        Logger::Warning("NLLB: Translate() called but worker not ready (loaded=" +
                        std::string(model_loaded_ ? "true" : "false") +
                        " running=" + std::string(running_ ? "true" : "false") + ")");
        return;
    }
    if (text.empty() || channel < 0 || channel >= CHANNEL_COUNT) return;

    std::string nllb_src = ToNLLBCode(src_lang);
    std::string nllb_tgt = ToNLLBCode(tgt_lang);
    Logger::Debug("NLLB: Queuing ch" + std::to_string(channel) + " " +
                  nllb_src + ">" + nllb_tgt + ": " + text.substr(0, 50));

    Request req;
    req.text = text;
    req.src_lang = nllb_src;
    req.tgt_lang = nllb_tgt;
    req.channel = channel;

    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        // Drop oldest if queue builds up
        while (requests_.size() >= 20) {
            requests_.pop();
        }
        requests_.push(std::move(req));
    }
    request_cv_.notify_one();
}

bool TranslationWorker::HasResult(int channel) const {
    if (channel < 0 || channel >= CHANNEL_COUNT) return false;
    std::lock_guard<std::mutex> lock(result_mutexes_[channel]);
    return !result_queues_[channel].empty();
}

std::string TranslationWorker::PopResult(int channel) {
    if (channel < 0 || channel >= CHANNEL_COUNT) return {};
    std::lock_guard<std::mutex> lock(result_mutexes_[channel]);
    if (result_queues_[channel].empty()) return {};
    std::string result = std::move(result_queues_[channel].front());
    result_queues_[channel].pop();
    return result;
}

void TranslationWorker::ProcessLoop() {
    while (running_) {
        Request req;
        {
            std::unique_lock<std::mutex> lock(request_mutex_);
            request_cv_.wait(lock, [this] {
                return !requests_.empty() || !running_;
            });
            if (!running_) break;
            req = std::move(requests_.front());
            requests_.pop();
        }

        try {
            // Tokenize with SentencePiece
            std::vector<std::string> tokens;
            tokenizer_->Encode(req.text, &tokens);

            if (tokens.empty()) continue;

            // NLLB format: [src_lang] + tokenized_text + [</s>]
            // The target language is passed as the target prefix
            std::vector<std::string> source_tokens;
            source_tokens.push_back(req.src_lang);
            source_tokens.insert(source_tokens.end(), tokens.begin(), tokens.end());
            source_tokens.push_back("</s>");

            // Target prefix: just the target language token
            std::vector<std::string> target_prefix = {req.tgt_lang};

            // Translate
            ctranslate2::TranslationOptions opts;
            opts.beam_size = 2;
            opts.max_decoding_length = 256;

            auto results = translator_->translate_batch(
                {source_tokens},
                {target_prefix},
                opts
            );
            if (results.empty() || results[0].hypotheses.empty()) continue;

            // Detokenize: join output tokens (skip the language prefix token)
            const auto& output_tokens = results[0].output();
            std::vector<std::string> text_tokens;
            for (const auto& tok : output_tokens) {
                // Skip NLLB language tokens and </s>
                if (tok == req.tgt_lang || tok == "</s>" || tok == "<s>") continue;
                text_tokens.push_back(tok);
            }

            std::string translated;
            tokenizer_->Decode(text_tokens, &translated);

            if (translated.empty()) continue;

#ifdef YIPOS_HAS_MECAB
            // Convert kanji to hiragana when translating to Japanese
            if (mecab_ && mecab_->IsAvailable() && req.tgt_lang == "jpn_Jpan") {
                translated = mecab_->KanjiToHiragana(translated);
            }
#endif

            Logger::Debug("NLLB: " + req.src_lang + ">" + req.tgt_lang +
                         ": \"" + req.text.substr(0, 40) + "\" -> \"" +
                         translated.substr(0, 40) + "\"");

            // Store latest for UI preview
            {
                std::lock_guard<std::mutex> lock(latest_mutexes_[req.channel]);
                latest_translated_[req.channel] = translated;
            }

            // Push to result queue
            {
                std::lock_guard<std::mutex> lock(result_mutexes_[req.channel]);
                auto& q = result_queues_[req.channel];
                while (q.size() >= MAX_RESULTS) q.pop();
                q.push(std::move(translated));
            }

        } catch (const std::exception& e) {
            std::string msg = e.what();
            Logger::Warning("NLLB: Translation failed: " + msg);

            // If CUDA fails at inference time, reload model on CPU
            if (device_name_ == "CUDA" &&
                (msg.find("cuda") != std::string::npos ||
                 msg.find("CUDA") != std::string::npos ||
                 msg.find("PTX") != std::string::npos)) {
                Logger::Warning("NLLB: CUDA inference failed, falling back to CPU");
                try {
                    translator_.reset();
                    translator_ = std::make_unique<ctranslate2::Translator>(
                        model_dir_, ctranslate2::Device::CPU, ctranslate2::ComputeType::AUTO);
                    device_name_ = "CPU";
                    Logger::Info("NLLB: Reloaded model on CPU successfully");
                } catch (const std::exception& e2) {
                    Logger::Error("NLLB: CPU fallback failed: " + std::string(e2.what()));
                    model_loaded_ = false;
                    running_ = false;
                }
            }
        }
    }
}

} // namespace YipOS
