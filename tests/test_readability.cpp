#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>

#include "content/readability.hpp"

using cstatic::compute_readability;
using cstatic::to_json;

TEST_CASE("Readability metrics", "[readability]") {

    SECTION("basic word count from HTML") {
        std::string html = "<p>The quick brown fox jumps.</p>";
        auto r = compute_readability(html);
        REQUIRE(r.word_count == 5);
    }

    SECTION("reading time is ceil(words / 200)") {
        // 5 words -> 1 minute.
        std::string html = "<p>The quick brown fox jumps.</p>";
        REQUIRE(compute_readability(html).reading_time_min == 1);

        // Build a string of ~250 distinct words to confirm the rounding.
        std::string body;
        for (int i = 0; i < 250; ++i) {
            body += "word ";
        }
        auto r = compute_readability("<p>" + body + "</p>");
        REQUIRE(r.word_count == 250);
        REQUIRE(r.reading_time_min == 2);  // ceil(250/200) = 2
    }

    SECTION("zero reading time for empty input") {
        REQUIRE(compute_readability("").reading_time_min == 0);
        REQUIRE(compute_readability("").word_count == 0);
    }

    SECTION("block <pre> code is not counted as prose") {
        std::string html =
            "<p>Intro.</p>"
            "<pre><code>int x = 42;\nfloat y = 3.14;</code></pre>"
            "<p>Outro.</p>";
        auto r = compute_readability(html);
        REQUIRE(r.word_count == 2);  // Intro + Outro
    }

    SECTION("inline <code> is stripped from prose") {
        std::string html = "<p>Use <code>printf</code> here.</p>";
        auto r = compute_readability(html);
        REQUIRE(r.word_count == 2);  // Use + here
    }

    SECTION("pure-code page has zero words") {
        std::string html = "<pre>int main() { return 0; }</pre>";
        auto r = compute_readability(html);
        REQUIRE(r.word_count == 0);
        REQUIRE(r.reading_time_min == 0);
    }

    SECTION("CJK ideographs count as one word each") {
        std::string html = "<p>\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E</p>";  // 日本語 (3 kanji)
        auto r = compute_readability(html);
        REQUIRE(r.word_count == 3);
    }

    SECTION("mixed Latin + CJK counts both") {
        // "Hello 世界." → Hello + 世 + 界 = 3 words.
        // Hello = 48 65 6C 6C 6F ; 世 = E4 B8 96 ; 界 = E7 95 8C
        std::string html = "<p>Hello \xE4\xB8\x96\xE7\x95\x8C.</p>";
        auto r = compute_readability(html);
        REQUIRE(r.word_count == 3);
    }

    SECTION("CJK-dominated text skips Flesch difficulty") {
        // Mostly-CJK prose: difficulty should be empty (Flesch is English-only).
        std::string html = "<p>\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x81\xAE\xE6\x96\x87\xE7\xAB\xA0</p>";
        auto r = compute_readability(html);
        REQUIRE(r.difficulty.empty());
    }

    SECTION("short simple English prose yields an 'easy' difficulty") {
        // Short sentences, short one-syllable words → high Flesch score.
        std::string html =
            "<p>The cat sat on the mat. The dog ran fast. "
            "The sun is hot. The sky is blue. "
            "We like to play. It is fun.</p>";
        auto r = compute_readability(html);
        REQUIRE(r.word_count > 0);
        REQUIRE(r.difficulty == "easy");
    }

    SECTION("HTML tags are stripped before counting") {
        std::string html = "<p>One Two Three Four Five.</p>";
        auto r = compute_readability(html);
        REQUIRE(r.word_count == 5);
    }

    SECTION("difficulty falls in the documented vocabulary") {
        // Sanity: difficulty (when non-empty) is one of the known labels.
        std::string html = "<p>Some prose here. More words follow. End.</p>";
        auto r = compute_readability(html);
        if (!r.difficulty.empty()) {
            REQUIRE((r.difficulty == "easy" || r.difficulty == "moderate" ||
                     r.difficulty == "difficult" || r.difficulty == "very-difficult"));
        }
    }

    SECTION("to_json produces expected shape") {
        std::string html = "<p>Hello world.</p>";
        auto r = compute_readability(html);
        nlohmann::json j = to_json(r);
        REQUIRE(j["word_count"] == 2);
        REQUIRE(j["reading_time"] == 1);
        REQUIRE(j.contains("difficulty"));
        REQUIRE(j["difficulty"].is_string());
    }

    SECTION("punctuation does not inflate word count") {
        std::string html = "<p>Yes! No? Maybe... Okay.</p>";
        auto r = compute_readability(html);
        REQUIRE(r.word_count == 4);  // Yes + No + Maybe + Okay
    }

    SECTION("decimal numbers are not split into sentence fragments") {
        // "3.14 is pi." — the period in 3.14 must not be a sentence boundary.
        // (Word counting splits on the period — The + value + 3 + 14 + is + pi = 6.
        // Sentence counting uses a whitespace lookahead so the decimal point
        // doesn't inflate the sentence count.)
        std::string html = "<p>The value 3.14 is pi.</p>";
        auto r = compute_readability(html);
        REQUIRE(r.word_count == 6);
        // Smoke check: difficulty was computed (at least one sentence found).
        REQUIRE_FALSE(r.difficulty.empty());
    }

    SECTION("whitespace-only HTML yields zero") {
        REQUIRE(compute_readability("   \n\t  ").word_count == 0);
    }
}
