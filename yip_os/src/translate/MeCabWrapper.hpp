#pragma once

#include <string>
#include <memory>

struct mecab_t;

namespace YipOS {

class MeCabWrapper {
public:
    MeCabWrapper();
    ~MeCabWrapper();

    // Initialize MeCab with system dictionary. Returns false if unavailable.
    bool Init();
    bool IsAvailable() const { return mecab_ != nullptr; }

    // Convert kanji in Japanese text to hiragana readings.
    // Kana, punctuation, and ASCII pass through unchanged.
    std::string KanjiToHiragana(const std::string& text) const;

private:
    mecab_t* mecab_ = nullptr;

    // Extract the reading field (katakana) from a MeCab feature CSV string.
    // Returns empty string if no reading is available.
    static std::string ExtractReading(const char* feature);

    // Convert a katakana string (UTF-8) to hiragana (UTF-8).
    static std::string KatakanaToHiragana(const std::string& katakana);

    // Check if a UTF-8 string segment contains any kanji (U+4E00-U+9FFF).
    static bool ContainsKanji(const char* text, size_t len);
};

} // namespace YipOS
