#include "MeCabWrapper.hpp"
#include "core/Logger.hpp"

#include <mecab.h>
#include <cstring>
#include <sstream>

namespace YipOS {

MeCabWrapper::MeCabWrapper() = default;

MeCabWrapper::~MeCabWrapper() {
    if (mecab_) {
        mecab_destroy(mecab_);
        mecab_ = nullptr;
    }
}

bool MeCabWrapper::Init() {
    mecab_ = mecab_new2("");
    if (!mecab_) {
        Logger::Warning("MeCab: Failed to initialize — " +
                        std::string(mecab_strerror(nullptr)));
        return false;
    }
    Logger::Info("MeCab: Initialized successfully");
    return true;
}

std::string MeCabWrapper::ExtractReading(const char* feature) {
    // ipadic feature format (comma-separated):
    //   0: 品詞 (POS)
    //   1-3: 品詞細分類 (POS subcategories)
    //   4: 活用型 (conjugation type)
    //   5: 活用形 (conjugation form)
    //   6: 原形 (base form)
    //   7: 読み (reading, katakana)
    //   8: 発音 (pronunciation)
    // We want field 7.
    int comma_count = 0;
    const char* start = nullptr;
    for (const char* p = feature; *p; ++p) {
        if (*p == ',') {
            comma_count++;
            if (comma_count == 7) {
                start = p + 1;
            } else if (comma_count == 8) {
                return std::string(start, p);
            }
        }
    }
    // If field 7 is the last field (no trailing comma)
    if (start) return std::string(start);
    return {};
}

bool MeCabWrapper::ContainsKanji(const char* text, size_t len) {
    for (size_t i = 0; i < len; ) {
        unsigned char c = text[i];
        uint32_t cp = 0;
        int bytes = 0;

        if (c < 0x80) {
            i++; continue;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F; bytes = 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F; bytes = 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07; bytes = 4;
        } else {
            i++; continue;
        }

        if (i + bytes > len) break;
        for (int j = 1; j < bytes; j++)
            cp = (cp << 6) | (text[i + j] & 0x3F);
        i += bytes;

        // CJK Unified Ideographs
        if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
        // CJK Unified Ideographs Extension A
        if (cp >= 0x3400 && cp <= 0x4DBF) return true;
    }
    return false;
}

std::string MeCabWrapper::KatakanaToHiragana(const std::string& katakana) {
    std::string result;
    result.reserve(katakana.size());
    for (size_t i = 0; i < katakana.size(); ) {
        unsigned char c = katakana[i];
        if ((c & 0xF0) == 0xE0 && i + 2 < katakana.size()) {
            // 3-byte UTF-8 sequence
            uint32_t cp = ((c & 0x0F) << 12) |
                          ((katakana[i+1] & 0x3F) << 6) |
                          (katakana[i+2] & 0x3F);

            // Katakana U+30A1-U+30F6 → Hiragana U+3041-U+3096
            if (cp >= 0x30A1 && cp <= 0x30F6) {
                cp -= 0x60;
                result += (char)(0xE0 | ((cp >> 12) & 0x0F));
                result += (char)(0x80 | ((cp >> 6) & 0x3F));
                result += (char)(0x80 | (cp & 0x3F));
                i += 3;
                continue;
            }
        }
        // Pass through unchanged
        result += katakana[i];
        i++;
    }
    return result;
}

std::string MeCabWrapper::KanjiToHiragana(const std::string& text) const {
    if (!mecab_) return text;

    const mecab_node_t* node = mecab_sparse_tonode(mecab_, text.c_str());
    if (!node) {
        Logger::Warning("MeCab: parseToNode failed");
        return text;
    }

    std::string result;
    result.reserve(text.size() * 2); // readings are often longer

    for (; node; node = node->next) {
        // Skip BOS and EOS nodes
        if (node->stat == MECAB_BOS_NODE || node->stat == MECAB_EOS_NODE)
            continue;

        std::string surface(node->surface, node->length);

        // Only convert if the surface contains kanji
        if (ContainsKanji(node->surface, node->length)) {
            std::string reading = ExtractReading(node->feature);
            if (!reading.empty() && reading != "*") {
                // MeCab returns readings in katakana — convert to hiragana
                result += KatakanaToHiragana(reading);
                continue;
            }
        }

        // No kanji or no reading available — pass through
        result += surface;
    }

    return result;
}

} // namespace YipOS
