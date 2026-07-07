#include "content/link_graph.hpp"
#include "utils/slugify.hpp"
#include "utils/terminal.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <regex>
#include <sstream>

namespace cstatic {

namespace {

// Lowercase ASCII in place. Non-ASCII bytes pass through unchanged.
std::string to_lower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// Slugify is now in utils/slugify.hpp (shared with G8 passage index + G11 TOC).
using utils::slugify;

// Extract the filename-stem segment from a URL.
//   "/posts/hello/" -> "hello"
//   "/about/"       -> "about"
//   "/"             -> ""  (skip; root has no stem)
//   "/404.html"     -> "404.html" (extension kept for non-clean URLs)
std::string url_stem(const std::string& url) {
    std::string s = url;
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.empty()) return "";
    auto pos = s.rfind('/');
    if (pos == std::string::npos) return s;
    return s.substr(pos + 1);
}

// Match `[[target]]` or `[[target|display]]`. Group 1 = target, group 2 =
// optional display. Target excludes `]`, `|`; display excludes `]`.
const std::regex& wikilink_re() {
    static const std::regex re(R"(\[\[([^\]|]+)(?:\|([^\]]+))?\]\])");
    return re;
}

} // anonymous namespace

void LinkGraph::index_page(const std::string& url,
                           const std::string& title,
                           const std::vector<std::string>& aliases) {
    if (url.empty()) return;

    // Filename-stem index (e.g. "/posts/hello/" -> "hello" -> url).
    // Last-writer-wins if two pages share a stem, which should be rare.
    std::string stem = url_stem(url);
    if (!stem.empty()) {
        slug_to_url_[stem] = url;
    }

    // Slugify-title index (catches [[Hello World]] when no stem matches).
    if (!title.empty()) {
        std::string slug = slugify(title);
        if (!slug.empty()) {
            slug_to_url_[slug] = url;
        }
        title_to_url_[to_lower(title)] = url;
    }

    // Aliases — exact-string matches authors can opt into via frontmatter.
    for (const auto& a : aliases) {
        if (!a.empty()) alias_to_url_[a] = url;
    }

    // Remember the display title for backlink rendering.
    url_to_title_[url] = title;
}

std::string LinkGraph::resolve(const std::string& target) const {
    if (target.empty()) return "";

    // 1. Exact filename-stem match (target as typed).
    {
        auto it = slug_to_url_.find(target);
        if (it != slug_to_url_.end()) return it->second;
    }

    // 2. Slugified target against stem/slug map.
    {
        std::string slug = slugify(target);
        if (slug != target) {
            auto it = slug_to_url_.find(slug);
            if (it != slug_to_url_.end()) return it->second;
        }
    }

    // 3. Lowercased target against lowercase-title map.
    {
        auto it = title_to_url_.find(to_lower(target));
        if (it != title_to_url_.end()) return it->second;
    }

    // 4. Exact alias match.
    {
        auto it = alias_to_url_.find(target);
        if (it != alias_to_url_.end()) return it->second;
    }

    return "";
}

std::string LinkGraph::title_for_url(const std::string& url) const {
    auto it = url_to_title_.find(url);
    if (it != url_to_title_.end()) return it->second;
    return url;
}

std::vector<Wikilink> LinkGraph::rewrite_wikilinks(std::string& markdown,
                                                    const std::string& source_url) const {
    std::vector<Wikilink> encountered;
    if (markdown.empty()) return encountered;

    const std::regex& re = wikilink_re();
    std::string out;
    out.reserve(markdown.size());

    // Manual cursor — see the shortcodes.cpp precedent: sregex_iterator
    // can swallow inner matches when blocks nest, and a moving regex_search
    // is easier to reason about for one-shot replacements. Use cbegin/cend
    // because markdown is mutable here; std::smatch wants const_iterators.
    size_t pos = 0;
    while (pos < markdown.size()) {
        std::smatch m;
        auto search_start = markdown.cbegin() + pos;
        if (!std::regex_search(search_start, markdown.cend(), m, re)) {
            out.append(markdown, pos, std::string::npos);
            break;
        }
        size_t match_pos = pos + m.position();

        // Append literal text preceding the match.
        out.append(markdown, pos, match_pos - pos);

        Wikilink link;
        link.target = m[1].str();
        // Trim whitespace around target/display for tolerance.
        auto trim = [](std::string s) {
            auto a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return std::string{};
            auto b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        };
        link.target = trim(link.target);
        link.display = m[2].matched ? trim(m[2].str()) : link.target;
        if (link.display.empty()) link.display = link.target;
        link.resolved_url = resolve(link.target);

        if (link.resolved_url.empty()) {
            std::cerr << utils::warning_label() << " unresolved wikilink '"
                      << link.target << "' in " << source_url << "\n";
            // No href on unresolved links; class lets templates/CSS style them.
            out += "<a class=\"wikilink-unresolved\">";
            out += link.display;
            out += "</a>";
        } else {
            out += "<a href=\"";
            out += link.resolved_url;
            out += "\">";
            out += link.display;
            out += "</a>";
        }

        encountered.push_back(std::move(link));
        pos = match_pos + m[0].length();
    }

    markdown = std::move(out);
    return encountered;
}

void LinkGraph::add_outgoing(const std::string& source_url,
                             const std::vector<Wikilink>& links) {
    if (source_url.empty()) return;
    auto& edges = outgoing_[source_url];
    for (const auto& link : links) {
        if (link.resolved_url.empty()) continue;
        // Use the indexed title for the target when available, falling back
        // to the wikilink display text.
        std::string target_title = title_for_url(link.resolved_url);
        if (target_title == link.resolved_url) target_title = link.display;
        edges.emplace_back(link.resolved_url, target_title);
    }
}

nlohmann::json LinkGraph::get_backlinks(const std::string& url) const {
    nlohmann::json result = nlohmann::json::array();
    if (url.empty()) return result;

    // Reverse scan: for each source page's outgoing edges, if any edge
    // points at `url`, that source is a backlink. Self-links excluded.
    // std::map iterates in sorted key order so the collected set is
    // deterministic; we then sort by title for stable template output.
    for (const auto& [source_url, edges] : outgoing_) {
        if (source_url == url) continue;
        bool links_here = false;
        for (const auto& [target_url, target_title] : edges) {
            if (target_url == url) { links_here = true; break; }
        }
        if (!links_here) continue;

        nlohmann::json entry;
        entry["url"] = source_url;
        entry["title"] = title_for_url(source_url);
        result.push_back(std::move(entry));
    }

    std::sort(result.begin(), result.end(),
        [](const nlohmann::json& a, const nlohmann::json& b) {
            return a.value("title", "") < b.value("title", "");
        });
    return result;
}

std::string LinkGraph::serialize_index() const {
    // std::map iterates in sorted key order, so output is deterministic
    // for a given set of indexed pages. Section markers make the format
    // self-describing and resistant to key collisions across maps.
    //
    // url_to_title_ is included even though it isn't a resolver map: a pure
    // case-only title change ("Alpha" -> "ALPHA") leaves the lowercase
    // resolver keys unchanged, but the display text shown in other pages'
    // backlinks would change. Hashing the title map catches that.
    std::ostringstream ss;
    ss << "slug:\n";
    for (const auto& [k, v] : slug_to_url_) ss << k << '|' << v << '\n';
    ss << "title:\n";
    for (const auto& [k, v] : title_to_url_) ss << k << '|' << v << '\n';
    ss << "alias:\n";
    for (const auto& [k, v] : alias_to_url_) ss << k << '|' << v << '\n';
    ss << "url_title:\n";
    for (const auto& [k, v] : url_to_title_) ss << k << '|' << v << '\n';
    return ss.str();
}

} // namespace cstatic
