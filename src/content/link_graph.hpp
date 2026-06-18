#pragma once

#include <string>
#include <vector>
#include <map>
#include <utility>

#include <nlohmann/json.hpp>

namespace cstatic {

// A single `[[target]]` or `[[target|display]]` occurrence. `resolved_url`
// is empty when the target could not be matched against any indexed page.
struct Wikilink {
    std::string target;        // raw target as typed between [[ and ]]
    std::string display;       // display text (target if no pipe)
    std::string resolved_url;  // empty if unresolved
};

// Resolves `[[wikilinks]]` to `<a href>` during the render phase and exposes
// the reverse graph (backlinks) to page templates.
//
// Usage flow in the builder:
//   1. Phase 1a: call `index_page()` for every page so the resolver maps
//      are populated (stem -> url, lowercase(title) -> url, alias -> url).
//   2. Phase 1b: call `rewrite_wikilinks()` on each page body before
//      `render_markdown`; pass the returned outgoing edges to
//      `add_outgoing()`. The emitted raw HTML survives cmark-gfm because
//      `CMARK_OPT_UNSAFE` is enabled in the markdown renderer.
//   3. Phase 2 (read-only): call `get_backlinks()` per page. Safe to call
//      from the multi-threaded render loop because no writes occur.
//
// Resolution order for a target:
//   1. Exact match against the filename-stem map (target as typed).
//   2. Slugified target against the same map (catches [[Hello World]]).
//   3. Lowercased target against the lowercase-title map.
//   4. Exact match against the alias map.
//
// Unresolved targets render as `<a class="wikilink-unresolved">display</a>`
// (no href) and emit a one-line warning on stderr.
class LinkGraph {
public:
    // Index one page by its resolved URL and title. Aliases are optional
    // additional frontmatter aliases that should also resolve to this URL.
    void index_page(const std::string& url,
                    const std::string& title,
                    const std::vector<std::string>& aliases = {});

    // Rewrite every `[[...]]` wikilink in `markdown` into HTML `<a>`,
    // mutating `markdown` in place. Returns the list of wikilinks
    // encountered (with `resolved_url` populated for the ones that matched).
    // `source_url` is used only for unresolved-target warning messages.
    std::vector<Wikilink> rewrite_wikilinks(std::string& markdown,
                                            const std::string& source_url) const;

    // Record the outgoing edges of a page. Only links with a non-empty
    // `resolved_url` contribute to the reverse graph; unresolved edges are
    // ignored.
    void add_outgoing(const std::string& source_url,
                      const std::vector<Wikilink>& links);

    // Return the backlinks of `url` as a JSON array of
    // `{ "url": "...", "title": "..." }` objects, sorted by source title
    // for deterministic output. Self-links are excluded.
    nlohmann::json get_backlinks(const std::string& url) const;

    // Deterministic serialization of the resolver maps for the incremental
    // cache hash. Same inputs produce identical output (maps iterate in
    // sorted key order).
    std::string serialize_index() const;

private:
    // Try to resolve a target string to a URL using the resolver maps.
    // Returns empty string if unresolved.
    std::string resolve(const std::string& target) const;

    // Lookup title by URL with URL fallback (used for backlink display).
    std::string title_for_url(const std::string& url) const;

    std::map<std::string, std::string> slug_to_url_;   // filename stem OR slugify(title) -> URL
    std::map<std::string, std::string> title_to_url_;  // lowercase(title) -> URL
    std::map<std::string, std::string> alias_to_url_;  // alias -> URL
    std::map<std::string, std::string> url_to_title_;  // URL -> resolved display title
    // source URL -> list of (target_url, target_title) for resolved edges
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> outgoing_;
};

} // namespace cstatic
