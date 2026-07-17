#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace cstatic {

// Processes `{% sources %}...{% endsources %}` blocks in markdown. Each block
// emits BOTH a visible numbered source list (an <ol class="sources"> with one
// <li> per source, passed through cmark-gfm via CMARK_OPT_UNSAFE) AND a
// JSON-LD CreativeWork object with a `citations` array. The caller is expected
// to merge the returned schemas into the page's `schema_extra` frontmatter so
// the seo_schema module (G3) emits them as additional
// <script type="application/ld+json"> blocks.
//
// Each inner line is one of:
//   - `- [text](url)` markdown link (optional trailing annotation is kept in
//     the visible HTML only; never copied into the citation's `name`)
//   - `- https://example.com/...` bare URL (autolinked; the citation's `name`
//     is omitted so the URL is not duplicated)
// Empty lines, list markers (`-`/`*`/`+`), and unrecognized lines are skipped.
// Blocks do not nest. Multiple blocks per page are supported: each emits its
// own CreativeWork schema, and every recognized entry is also appended to
// `out_sources_ctx` (a JSON array of {text, url, note} objects) so templates
// can render `{{ page.sources }}`.
//
// Usage:
//   SourcesBlockProcessor sbp;
//   std::vector<nlohmann::json> schemas;
//   nlohmann::json sources_ctx;
//   body = sbp.process(body, schemas, sources_ctx);
class SourcesBlockProcessor {
public:
    SourcesBlockProcessor();

    // Returns the markdown with every sources block replaced by its visible
    // HTML. Extracted JSON-LD CreativeWork objects are appended to
    // `out_schemas` (each is a complete schema with @context + @type). Every
    // recognized source entry is appended to `out_sources_ctx` (which is
    // treated as a JSON array: if it is not already an array it is reset to
    // an empty array on entry).
    std::string process(const std::string& markdown,
                        std::vector<nlohmann::json>& out_schemas,
                        nlohmann::json& out_sources_ctx) const;
};

} // namespace cstatic
