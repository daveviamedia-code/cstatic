#include "content/passage_index.hpp"
#include "utils/file_io.hpp"
#include "utils/slugify.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <unordered_map>

namespace cstatic {

namespace {

// Collapse runs of ASCII whitespace into single spaces. Mirrors the
// anonymous helper in llms_txt.cpp so passage text stays on one line.
std::string collapse_ws(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool prev_ws = false;
    for (char c : s) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ' || c == '\v' || c == '\f') {
            if (!prev_ws) { out.push_back(' '); prev_ws = true; }
        } else {
            out.push_back(c);
            prev_ws = false;
        }
    }
    // trim trailing space if any
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Match <h[2-6]>...</h[2-6]>. Case-insensitive so it survives any future
// uppercased heading output. Group 1 = level digit, group 2 = inner HTML.
// `[^>]*` between tag name and `>` absorbs attribute strings (class, id).
const std::regex& heading_re() {
    static const std::regex re(R"(<h([2-6])[^>]*>(.*?)</h\1>)",
                               std::regex::ECMAScript | std::regex::icase);
    return re;
}

} // anonymous namespace

std::vector<Passage> extract_passages(const std::string& html) {
    std::vector<Passage> out;
    if (html.empty()) return out;

    const std::regex& re = heading_re();

    // First pass: collect heading matches (position, level, inner HTML) so
    // we can compute each body as [current heading end, next heading start).
    struct HeadingMatch {
        size_t match_start;
        size_t match_end;
        int level;
        std::string inner;
    };
    std::vector<HeadingMatch> headings;

    {
        size_t pos = 0;
        while (pos < html.size()) {
            std::smatch m;
            auto search_start = html.cbegin() + pos;
            if (!std::regex_search(search_start, html.cend(), m, re)) break;
            size_t abs_pos = pos + static_cast<size_t>(m.position());
            size_t abs_end = abs_pos + static_cast<size_t>(m.length());
            HeadingMatch hm;
            hm.match_start = abs_pos;
            hm.match_end   = abs_end;
            hm.level       = std::stoi(m[1].str());
            hm.inner       = m[2].str();
            headings.push_back(std::move(hm));
            pos = abs_end;
        }
    }

    if (headings.empty()) return out;

    // Slug collision handler: first occurrence keeps the slug, subsequent
    // ones get -1, -2, ... suffixes. Matches GitHub/MDX conventions so
    // in-page anchors stay stable and unambiguous.
    std::unordered_map<std::string, int> seen;

    out.reserve(headings.size());
    for (size_t i = 0; i < headings.size(); ++i) {
        const auto& hm = headings[i];

        Passage p;
        p.level = hm.level;

        // Heading text: strip nested tags (e.g. <a>...</a>), collapse ws.
        std::string raw_heading = utils::strip_html_tags(hm.inner);
        p.heading = collapse_ws(raw_heading);

        // ID: slugify the heading text. G11 (auto TOC) injects matching
        // id attrs using this same util so anchor links line up.
        std::string slug = utils::slugify(p.heading);
        if (slug.empty()) {
            // Heading was entirely punctuation/whitespace — synthesize a
            // stable placeholder so hasPart urls still resolve.
            slug = "section";
        }
        auto it = seen.find(slug);
        if (it == seen.end() || it->second == 0) {
            seen[slug] = 1;
        } else {
            slug += "-" + std::to_string(it->second);
            ++it->second;
        }
        p.id = slug;

        // Body: substring from end of this heading to start of next (or EOF).
        size_t body_start = hm.match_end;
        size_t body_end   = (i + 1 < headings.size())
                            ? headings[i + 1].match_start
                            : html.size();
        std::string body_html = html.substr(body_start,
                                            body_end < body_start ? 0
                                              : body_end - body_start);
        std::string body_text = collapse_ws(utils::strip_html_tags(body_html));
        p.text = utils::truncate_text(body_text, 500);

        out.push_back(std::move(p));
    }

    return out;
}

nlohmann::json to_json(const std::vector<Passage>& passages) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : passages) {
        nlohmann::json o;
        o["id"]      = p.id;
        o["heading"] = p.heading;
        o["text"]    = p.text;
        o["level"]   = p.level;
        arr.push_back(std::move(o));
    }
    return arr;
}

} // namespace cstatic
