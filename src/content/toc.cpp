#include "content/toc.hpp"
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
// helper in passage_index.cpp so heading text stays on one line.
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
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Match <h[2-6]>...</h[2-6]>. Same regex as passage_index (G8).
// Group 1 = level digit, group 2 = inner HTML.
const std::regex& heading_re() {
    static const std::regex re(R"(<h([2-6])[^>]*>(.*?)</h\1>)",
                               std::regex::ECMAScript | std::regex::icase);
    return re;
}

// Extract id="value" from an opening-tag substring. Requires whitespace
// before 'id' so it won't match 'data-id' or similar compound attributes.
const std::regex& id_attr_re() {
    static const std::regex re(R"RE(\sid\s*=\s*"([^"]*)")RE",
                                std::regex::ECMAScript | std::regex::icase);
    return re;
}

// Collected heading info from the first pass.
struct HeadingInfo {
    size_t tag_start   = 0;  // absolute position of '<' in html
    size_t opening_gt  = 0;  // absolute position of '>' closing the opening tag
    int    level       = 0;
    std::string inner;       // inner HTML between tags
    std::string text;        // heading text (tags stripped, ws collapsed)
    std::string existing_id; // non-empty if heading already has id="..."
    std::string final_id;    // computed in second pass
};

// Recursive tree builder. Consumes headings from idx, returns siblings
// whose level is strictly greater than parent_level. Handles skip-level
// cases (e.g. h2 -> h4) by nesting the h4 directly under the h2.
std::vector<TocEntry> build_level(size_t& idx,
                                   const std::vector<HeadingInfo>& headings,
                                   int parent_level) {
    std::vector<TocEntry> result;
    while (idx < headings.size() && headings[idx].level > parent_level) {
        TocEntry entry;
        entry.id    = headings[idx].final_id;
        entry.text  = headings[idx].text;
        entry.level = headings[idx].level;
        int my_level = headings[idx].level;
        idx++;
        entry.children = build_level(idx, headings, my_level);
        result.push_back(std::move(entry));
    }
    return result;
}

void render_level(std::string& out, const std::vector<TocEntry>& toc) {
    out += "<ul>\n";
    for (const auto& e : toc) {
        out += "<li class=\"toc-level-";
        out += std::to_string(e.level);
        out += "\"><a href=\"#";
        out += e.id;
        out += "\">";
        out += e.text;
        out += "</a>";
        if (!e.children.empty()) {
            out += '\n';
            render_level(out, e.children);
        }
        out += "</li>\n";
    }
    out += "</ul>\n";
}

nlohmann::json entries_to_json(const std::vector<TocEntry>& toc) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : toc) {
        nlohmann::json o;
        o["id"]       = e.id;
        o["text"]     = e.text;
        o["level"]    = e.level;
        o["children"] = entries_to_json(e.children);
        arr.push_back(std::move(o));
    }
    return arr;
}

} // anonymous namespace

std::vector<TocEntry> build_toc(std::string& html_content) {
    if (html_content.empty()) return {};

    const std::regex& re = heading_re();

    // -- First pass: collect all heading matches.
    std::vector<HeadingInfo> headings;
    {
        size_t pos = 0;
        while (pos < html_content.size()) {
            std::smatch m;
            auto search_start = html_content.cbegin() + pos;
            if (!std::regex_search(search_start, html_content.cend(), m, re)) break;

            size_t abs_pos = pos + static_cast<size_t>(m.position());
            size_t abs_end = abs_pos + static_cast<size_t>(m.length());

            HeadingInfo hi;
            hi.tag_start = abs_pos;
            hi.level     = std::stoi(m[1].str());
            hi.inner     = m[2].str();

            // Locate the '>' that closes the opening tag (first '>' in
            // the full match — the regex guarantees it ends the tag attrs).
            const std::string& full = m[0].str();
            size_t gt_in_match = full.find('>');
            hi.opening_gt = abs_pos + gt_in_match;

            // Check for an existing id attribute in the opening tag.
            std::string opening_tag = full.substr(0, gt_in_match + 1);
            std::smatch id_m;
            if (std::regex_search(opening_tag, id_m, id_attr_re())) {
                hi.existing_id = id_m[1].str();
            }

            headings.push_back(std::move(hi));
            pos = abs_end;
        }
    }

    if (headings.empty()) return {};

    // -- Second pass: compute heading text and final IDs.
    // Collision logic mirrors G8 passage_index exactly so anchor IDs align:
    // first occurrence keeps the slug, subsequent get -1, -2, ...
    std::unordered_map<std::string, int> seen;
    for (auto& h : headings) {
        std::string raw = utils::strip_html_tags(h.inner);
        h.text = collapse_ws(raw);

        if (!h.existing_id.empty()) {
            h.final_id = h.existing_id;
            continue;
        }

        std::string slug = utils::slugify(h.text);
        if (slug.empty()) slug = "section";
        auto it = seen.find(slug);
        if (it == seen.end() || it->second == 0) {
            seen[slug] = 1;
        } else {
            slug += "-" + std::to_string(it->second);
            ++it->second;
        }
        h.final_id = slug;
    }

    // -- Third pass: inject id="..." into headings that lack one.
    // Process in reverse so earlier offsets stay valid after insertions.
    for (auto it = headings.rbegin(); it != headings.rend(); ++it) {
        if (!it->existing_id.empty()) continue;
        std::string insertion = " id=\"" + it->final_id + "\"";
        html_content.insert(it->opening_gt, insertion);
    }

    // -- Build the nested tree.
    size_t idx = 0;
    return build_level(idx, headings, 1);  // parent_level=1 -> collect h2+
}

nlohmann::json to_json(const std::vector<TocEntry>& toc) {
    return entries_to_json(toc);
}

std::string render_toc_html(const std::vector<TocEntry>& toc) {
    if (toc.empty()) return "";
    std::string out = "<nav class=\"toc\">\n";
    render_level(out, toc);
    out += "</nav>\n";
    return out;
}

void replace_toc_markers(std::string& html_content,
                          const std::vector<TocEntry>& toc) {
    if (toc.empty()) return;

    std::regex marker_re(R"(<!--\s*toc\s*-->)", std::regex::icase);
    std::string rendered = render_toc_html(toc);

    // Manual replace-all loop (avoids regex_replace backreference pitfalls
    // when the replacement text contains '$' or '\').
    std::string out;
    out.reserve(html_content.size() + rendered.size());

    auto it    = html_content.cbegin();
    auto end   = html_content.cend();

    while (it != end) {
        std::smatch m;
        if (!std::regex_search(it, end, m, marker_re)) {
            out.append(it, end);
            break;
        }
        // Append text before the marker.
        out.append(it, it + m.position());
        // Append rendered TOC.
        out += rendered;
        // Advance past the marker.
        it += m.position() + m.length();
    }
    html_content = std::move(out);
}

} // namespace cstatic
