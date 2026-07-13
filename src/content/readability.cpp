#include "content/readability.hpp"
#include "utils/file_io.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>

namespace cstatic {

namespace {

// Words-per-minute constant for reading-time estimation. 200 is the
// commonly cited average adult reading speed for English prose.
constexpr int WORDS_PER_MINUTE = 200;

// Match a <pre>...</pre> or <code>...</code> block (case-insensitive,
// spans newlines via [\s\S]). We strip these before counting because
// code isn't prose and skews syllable counts badly (no vowels, weird
// word shapes). Uses a custom raw-string delimiter because the pattern
// contains `)"` which would terminate a bare `R"(...)"`.
const std::regex& code_block_re() {
    static const std::regex re(
        R"RE(<(?:pre|code)\b[^>]*>[\s\S]*?</(?:pre|code)>)RE",
        std::regex::ECMAScript | std::regex::icase);
    return re;
}

// True for ASCII whitespace (space, tab, newline, CR, vertical tab,
// form feed). We don't rely on isspace() because UTF-8 non-breaking
// space (U+00A0) decodes to a non-ASCII byte sequence we'd rather treat
// as a word character than split on.
bool is_ascii_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\r' || c == '\v' || c == '\f';
}

// True for ASCII vowels (used by the syllable counter — Flesch is an
// English-specific heuristic, so non-ASCII letters just don't count).
bool is_vowel(char c) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return c == 'a' || c == 'e' || c == 'i' ||
           c == 'o' || c == 'u' || c == 'y';
}

// Decode the UTF-8 character starting at s[i]. Returns the code point
// and sets `consumed` to the byte length. Returns 0 and consumes 1 byte
// for invalid sequences (so the caller makes progress).
unsigned decode_utf8(const std::string& s, size_t i, size_t& consumed) {
    if (i >= s.size()) { consumed = 0; return 0; }
    unsigned char b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) { consumed = 1; return b0; }  // ASCII
    // 2-byte: 110xxxxx 10xxxxxx
    if ((b0 & 0xE0) == 0xC0 && i + 1 < s.size() &&
        (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) {
        consumed = 2;
        return ((b0 & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
    }
    // 3-byte: 1110xxxx 10xxxxxx 10xxxxxx
    if ((b0 & 0xF0) == 0xE0 && i + 2 < s.size() &&
        (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80 &&
        (static_cast<unsigned char>(s[i + 2]) & 0xC0) == 0x80) {
        consumed = 3;
        return ((b0 & 0x0F) << 12) |
               ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
               (static_cast<unsigned char>(s[i + 2]) & 0x3F);
    }
    // 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if ((b0 & 0xF8) == 0xF0 && i + 3 < s.size() &&
        (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80 &&
        (static_cast<unsigned char>(s[i + 2]) & 0xC0) == 0x80 &&
        (static_cast<unsigned char>(s[i + 3]) & 0xC0) == 0x80) {
        consumed = 4;
        return ((b0 & 0x07) << 18) |
               ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
               ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
               (static_cast<unsigned char>(s[i + 3]) & 0x3F);
    }
    consumed = 1;
    return 0;
}

// True if a Unicode code point is a CJK ideograph or kana. Each of these
// counts as one "word" since CJK text isn't whitespace-separated.
bool is_cjk(unsigned cp) {
    return (cp >= 0x3040 && cp <= 0x30FF)   // Hiragana / Katakana
        || (cp >= 0x3400 && cp <= 0x4DBF)   // CJK Extension A
        || (cp >= 0x4E00 && cp <= 0x9FFF)   // CJK Unified Ideographs
        || (cp >= 0xAC00 && cp <= 0xD7AF)   // Hangul Syllables
        || (cp >= 0xF900 && cp <= 0xFAFF);  // CJK Compatibility Ideographs
}

// Count syllables in a single ASCII-alpha word using the vowel-group
// heuristic: each run of consecutive vowels counts as one syllable, a
// trailing silent 'e' doesn't count ("made" → 1, "the" → 1), minimum 1.
int count_syllables(const std::string& word) {
    int groups = 0;
    bool in_vowels = false;
    for (char c : word) {
        bool v = is_vowel(c);
        if (v && !in_vowels) { ++groups; in_vowels = true; }
        else if (!v) { in_vowels = false; }
    }
    if (groups > 1 && !word.empty() &&
        std::tolower(static_cast<unsigned char>(word.back())) == 'e') {
        --groups;
    }
    return groups < 1 ? 1 : groups;
}

// Count sentence terminators (`.`, `!`, `?`) followed by whitespace or
// end-of-string. The lookahead skips decimal points like "3.14". Falls
// back to 1 when none are found so the Flesch ratio doesn't divide by 0.
int count_sentences(const std::string& text) {
    int n = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '.' || c == '!' || c == '?') {
            if (i + 1 >= text.size() ||
                is_ascii_ws(static_cast<unsigned char>(text[i + 1]))) {
                ++n;
            }
        }
    }
    return n < 1 ? 1 : n;
}

// Map a Flesch reading-ease score to a coarse difficulty label.
// Returns empty string when score <= 0 (sentinel for not-computable).
std::string difficulty_from_score(double score) {
    if (score <= 0.0) return "";
    if (score >= 70.0) return "easy";
    if (score >= 50.0) return "moderate";
    if (score >= 30.0) return "difficult";
    return "very-difficult";
}

} // anonymous namespace

Readability compute_readability(const std::string& html) {
    Readability r;
    if (html.empty()) return r;

    // Strip <pre>/<code> blocks first, then all remaining HTML tags.
    std::string stripped = std::regex_replace(html, code_block_re(), std::string(" "));
    std::string text = utils::strip_html_tags(stripped);
    if (text.empty()) return r;

    int words = 0;
    int cjk_chars = 0;
    int syllables = 0;
    int ascii_words_for_syllables = 0;

    std::string current_word;
    auto flush_word = [&]() {
        if (current_word.empty()) return;
        ++words;
        // Only feed all-alpha ASCII words to the syllable counter —
        // Flesch is an English heuristic, digits/punctuation skew it.
        bool all_alpha = true;
        for (char c : current_word) {
            if (!std::isalpha(static_cast<unsigned char>(c))) {
                all_alpha = false;
                break;
            }
        }
        if (all_alpha) {
            syllables += count_syllables(current_word);
            ++ascii_words_for_syllables;
        }
        current_word.clear();
    };

    for (size_t i = 0; i < text.size(); ) {
        size_t consumed = 0;
        unsigned cp = decode_utf8(text, i, consumed);
        if (consumed == 0) { ++i; continue; }

        if (cp < 0x80) {
            unsigned char c = static_cast<unsigned char>(cp);
            if (is_ascii_ws(c)) {
                flush_word();
            } else if (std::isalnum(c)) {
                current_word.push_back(static_cast<char>(c));
            } else {
                // Punctuation etc. — word boundary, don't accumulate.
                flush_word();
            }
        } else if (is_cjk(cp)) {
            flush_word();
            ++cjk_chars;
        } else {
            // Other non-ASCII (extended Latin, accented vowels, etc.).
            // Accumulate raw bytes so letters don't get split.
            for (size_t k = 0; k < consumed; ++k) {
                current_word.push_back(text[i + k]);
            }
        }
        i += consumed;
    }
    flush_word();

    // Each CJK char counts as one word (no whitespace separation).
    words += cjk_chars;

    r.word_count = words;
    if (words <= 0) return r;
    r.reading_time_min = (words + WORDS_PER_MINUTE - 1) / WORDS_PER_MINUTE;

    // Flesch reading ease only makes sense for English prose. Skip when
    // there are no ASCII words to count syllables on, or when CJK chars
    // dominate (>= half of the "words").
    if (ascii_words_for_syllables > 0 && cjk_chars * 2 <= words) {
        int sentences = count_sentences(text);
        double score = 206.835
                     - 1.015 * (static_cast<double>(words) / sentences)
                     - 84.6  * (static_cast<double>(syllables) / ascii_words_for_syllables);
        r.difficulty = difficulty_from_score(score);
    }

    return r;
}

nlohmann::json to_json(const Readability& r) {
    nlohmann::json j;
    j["word_count"]   = r.word_count;
    j["reading_time"] = r.reading_time_min;
    j["difficulty"]   = r.difficulty;
    return j;
}

} // namespace cstatic
