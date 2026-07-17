#include "content/sources_block.hpp"
#include "utils/file_io.hpp"
#include "utils/terminal.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cstatic {

namespace {

// Trim all leading/trailing ASCII whitespace (spaces, tabs, newlines).
std::string trim_ws(const std::string& s) {
    auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// A single recognized source entry. `note` is the trailing annotation text
// after a markdown link (empty for bare URLs or links without a suffix).
struct SourceEntry {
    std::string text;
    std::string url;
    std::string note;
};

// Parse the inner body of a `{% sources %}` block line-by-line. Recognized
// forms: `- [text](url)[ note]` and `- https://example.com/...`. List markers
// (`-`/`*`/`+`) are optional. Blank/unrecognized lines are skipped silently.
std::vector<SourceEntry> parse_sources(const std::string& inner) {
    static const std::regex link_re(R"RE(\[([^\]]+)\]\(([^)\s]+)\))RE");
    static const std::regex url_re(R"(^https?://\S+$)");

    std::vector<SourceEntry> out;
    std::istringstream ss(inner);
    std::string line;
    while (std::getline(ss, line)) {
        // Normalize trailing CR (CRLF inputs).
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string t = trim_ws(line);
        if (t.empty()) continue;

        // Strip a single optional leading list marker (-, *, or +).
        if (t.size() >= 2 && (t[0] == '-' || t[0] == '*' || t[0] == '+') && t[1] == ' ') {
            t = trim_ws(t.substr(2));
            if (t.empty()) continue;
        }

        SourceEntry e;
        std::smatch m;
        if (std::regex_search(t, m, link_re)) {
            e.text = m[1].str();
            e.url  = m[2].str();
            // Trailing annotation is whatever follows the link on the line.
            e.note = trim_ws(m.suffix().str());
        } else if (std::regex_match(t, url_re)) {
            // Bare URL: autolink; text equals url; citation's name is omitted
            // downstream to avoid duplicating the URL.
            e.text = t;
            e.url  = t;
            e.note.clear();
        } else {
            // Unrecognized line — skip silently.
            continue;
        }
        out.push_back(std::move(e));
    }
    return out;
}

// Render the visible <ol class="sources"> block and the CreativeWork citation
// schema for a single sources block. Returns the visible HTML; the schema is
// pushed into `out_schemas`. Returns an empty string (and pushes no schema)
// when `entries` is empty.
std::string render_sources(const std::vector<SourceEntry>& entries,
                           std::vector<nlohmann::json>& out_schemas) {
    if (entries.empty()) return "";

    nlohmann::json citations = nlohmann::json::array();
    std::string html = "<ol class=\"sources\">\n";
    for (const auto& e : entries) {
        std::string url_esc  = utils::xml_escape(e.url);
        std::string text_esc = utils::xml_escape(e.text);

        html += "<li>";
        html += "<a href=\"" + url_esc + "\">" + text_esc + "</a>";
        if (!e.note.empty()) {
            html += " " + utils::xml_escape(e.note);
        }
        html += "</li>\n";

        nlohmann::json cite;
        cite["@type"] = "CreativeWork";
        // For bare URLs e.text == e.url; omit `name` so we don't echo the URL.
        if (e.text != e.url) {
            cite["name"] = e.text;
        }
        cite["url"] = e.url;
        citations.push_back(std::move(cite));
    }
    html += "</ol>";

    nlohmann::json schema;
    schema["@context"]  = "https://schema.org";
    schema["@type"]     = "CreativeWork";
    schema["citations"] = std::move(citations);
    out_schemas.push_back(std::move(schema));

    return html;
}

} // anonymous namespace

SourcesBlockProcessor::SourcesBlockProcessor() = default;

std::string SourcesBlockProcessor::process(const std::string& markdown,
                                           std::vector<nlohmann::json>& out_schemas,
                                           nlohmann::json& out_sources_ctx) const {
    if (markdown.empty()) return markdown;

    if (!out_sources_ctx.is_array()) {
        out_sources_ctx = nlohmann::json::array();
    }

    static const std::regex open_re(R"RE(\{\%\s*sources\s*\%\})RE");
    static const std::regex close_re(R"(\{\%\s*endsources\s*\%\})");

    std::string out;
    out.reserve(markdown.size());
    size_t pos = 0;
    while (pos < markdown.size()) {
        std::smatch om;
        if (!std::regex_search(markdown.cbegin() + pos, markdown.cend(), om, open_re)) {
            out.append(markdown, pos, std::string::npos);
            break;
        }
        size_t open_pos = pos + om.position();
        size_t open_end  = open_pos + om[0].length();

        // Literal text preceding the block.
        out.append(markdown, pos, open_pos - pos);

        // Locate the matching close tag (blocks don't nest — first match wins).
        std::smatch cm;
        if (!std::regex_search(markdown.cbegin() + open_end, markdown.cend(),
                               cm, close_re)) {
            // Unclosed block — leave the opener verbatim and continue scanning.
            out += om[0].str();
            pos = open_end;
            continue;
        }
        size_t close_pos = open_end + cm.position();
        size_t close_end = close_pos + cm[0].length();
        std::string inner(markdown, open_end, close_pos - open_end);

        auto entries = parse_sources(inner);
        if (entries.empty()) {
            std::cerr << utils::warning_label()
                      << " sources block has no recognizable entries"
                         " — passing content through\n";
            // Strip the wrapper tags but leave the inner content for normal
            // markdown rendering; no schema is emitted.
            out += inner;
            pos = close_end;
            continue;
        }

        std::string html = render_sources(entries, out_schemas);

        // Also extend the template-facing ctx array.
        for (const auto& e : entries) {
            nlohmann::json ctx_entry;
            ctx_entry["text"] = e.text;
            ctx_entry["url"]  = e.url;
            ctx_entry["note"] = e.note;
            out_sources_ctx.push_back(std::move(ctx_entry));
        }

        // Blank-line separation so cmark-gfm treats the emitted HTML as a
        // raw HTML block (CMARK_OPT_UNSAFE passes it through verbatim).
        out += "\n\n";
        out += html;
        out += "\n\n";
        pos = close_end;
    }
    return out;
}

} // namespace cstatic
