#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace cstatic {

// G12 — Reading time / word count / difficulty. Cheap computed fields
// derived from a page's rendered HTML, exposed to templates as
// {{ page.word_count }}, {{ page.reading_time }}, {{ page.difficulty }}
// and emitted on the page's JSON-LD schema as `wordCount` and
// `timeRequired` (ISO 8601 duration `PT5M`).
//
// Always on (pure derived metadata, like excerpt, passages, and toc).
// JSON-LD emission of wordCount/timeRequired is gated by the existing
// seo.json_ld_enabled flag — the page context fields always render.
//
// Word counting handles both whitespace-separated Latin words and CJK
// ideographs (each CJK char counts as one word, since CJK text isn't
// whitespace-separated). Difficulty uses the Flesch reading-ease heuristic
// and is only computed for English-dominant prose (skipped when CJK chars
// dominate, since Flesch is an English-specific formula).
struct Readability {
    int word_count = 0;        // whitespace words + CJK chars
    int reading_time_min = 0;  // ceil(word_count / 200)
    std::string difficulty;    // "easy" | "moderate" | "difficult" | "very-difficult" | "" (not computable)
};

// Compute readability metrics from rendered HTML. Strips <pre>/<code>
// blocks first (code isn't prose and skews syllable counts), then strips
// remaining HTML tags. Returns a zeroed Readability when the page has no
// prose text.
Readability compute_readability(const std::string& html);

// Serialize a Readability to JSON: {word_count, reading_time, difficulty}.
nlohmann::json to_json(const Readability& r);

} // namespace cstatic
