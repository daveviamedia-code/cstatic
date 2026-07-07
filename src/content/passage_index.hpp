#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cstatic {

// G8 — Passage index. Extracts citable passages (heading + following body
// text) from a page's rendered HTML so AI engines (Google AI Overviews,
// ChatGPT web search, Perplexity) get machine-readable passage boundaries
// and anchor targets.
//
// A passage = one `<h2>`–`<h6>` heading plus the sibling text that follows
// it up to the next heading (h1 is skipped — it's the page title). Headings
// are slugified into IDs so G11 (auto TOC) can emit matching `#anchor` links
// once it lands. Duplicate headings get `-1`, `-2`, … suffixes.
//
// Always on (pure derived page metadata, like `excerpt`). JSON-LD emission
// of `hasPart` is gated by the existing `seo.json_ld_enabled` flag.
struct Passage {
    std::string id;        // slugified heading text
    std::string heading;   // heading text (HTML stripped)
    std::string text;      // body text until next heading (HTML stripped, ws collapsed, <=500 chars)
    int level = 0;         // 2-6
};

// Extract passages from rendered HTML. Returns an empty vector when the page
// has no <h2>-<h6> headings.
std::vector<Passage> extract_passages(const std::string& html);

// Serialize passages to a JSON array: [{id, heading, text, level}, ...].
nlohmann::json to_json(const std::vector<Passage>& passages);

} // namespace cstatic
