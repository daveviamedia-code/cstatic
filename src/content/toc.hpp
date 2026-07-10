#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cstatic {

// G11 — Auto Table of Contents. Builds a nested TOC tree from rendered
// HTML headings (h2–h6) and injects id="..." attributes into those heading
// tags so in-page #anchor links actually resolve (cmark-gfm doesn't emit
// id attributes by default).
//
// IDs are computed with utils::slugify — the SAME function G8 (passage
// index) uses — so a passage's hasPart URL (#intro) and the corresponding
// <h2 id="intro"> stay in sync. Duplicate headings get -1, -2, … suffixes,
// matching G8's collision logic exactly.
//
// Always on (pure derived metadata, like excerpt and passages). The TOC
// tree is exposed as {{ page.toc }} for template rendering.
struct TocEntry {
    std::string id;
    std::string text;
    int level = 0;   // 2–6
    std::vector<TocEntry> children;
};

// Build a TOC tree from rendered HTML headings (h2–h6). MUTATES
// html_content to inject id="..." attributes on headings that lack them.
// Headings that already have an id attribute are preserved verbatim.
// Returns an empty vector when the page has no h2–h6 headings.
std::vector<TocEntry> build_toc(std::string& html_content);

// Serialize TOC tree to JSON: [{id, text, level, children: [...]}, ...].
nlohmann::json to_json(const std::vector<TocEntry>& toc);

// Render a TOC tree as a <nav class="toc"><ul>…</ul></nav> HTML string.
std::string render_toc_html(const std::vector<TocEntry>& toc);

// Replace <!--toc--> (and <!-- toc -->) markers in html_content with the
// rendered TOC HTML. No-op when no marker is present.
void replace_toc_markers(std::string& html_content,
                          const std::vector<TocEntry>& toc);

} // namespace cstatic
