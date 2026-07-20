# C-Static: GEO Improvement Plan (v4)

## Context

C-Static's `seo_meta` currently emits only Open Graph + Twitter Card + canonical tags. It has no JSON-LD, no AI crawler handling, no `llms.txt`, no author/organization entity modeling, and no way to measure AI visibility. This plan covers every item needed to make C-Static sites first-class citable by Google AI Overviews, ChatGPT web search, Perplexity, Bing Copilot, Claude, and emerging agent platforms.

Items are ordered for implementation. Each tier depends only on prior tiers (and on the existing v3 modules). All features opt-in via config flags so existing builds are unaffected.

## Architecture Patterns to Reuse

- **Module pattern**: `void generate_X(const Config&, const nlohmann::json& pages, const std::string& output_dir)` — see `src/modules/rss.cpp`, `sitemap.cpp`, `json_feed.cpp`
- **Config parsing**: `optional_bool/string/int(tbl, "dotted.key", default)` in `src/config/config.cpp`; fields declared in `src/config/config.hpp`
- **SEO meta injection**: `build_seo_meta()` at `src/pipeline/builder.cpp:118`, called at lines 311 (markdown pages) and 1212 (data-driven pages)
- **CLI subcommand**: `app.add_subcommand()` + callback lambda in `src/main.cpp` (see `cmd_check`)
- **Shortcode processor**: `src/content/shortcodes.cpp` — `ShortcodeProcessor` runs in Phase 1b before `render_markdown`
- **External tool detection**: `tool_available()` + `ToolCache` + `run_converter()` — `src/assets/asset_pipeline.cpp`

---

## Tier 1 — Foundational Table-Stakes

### G1. AI Crawler Allowlist in robots.txt    DONE

**Approach**: Extend `src/modules/robots.cpp` to emit per-crawler `User-agent` blocks for known AI crawlers. Config selects allow / disallow / custom.

**Config — `src/config/config.hpp`** (after `[modules.robots]` block, ~line 99):
```cpp
// [modules.robots.ai_crawlers]
// "allow" | "disallow" | "custom". When "custom", uses ai_crawlers_custom list.
std::string robots_ai_crawlers_mode = "allow";
std::vector<std::string> robots_ai_crawlers_custom;  // agent names to allow
```

**Module change — `src/modules/robots.cpp`**:
- Add static `k_ai_agents` array: `GPTBot`, `OAI-SearchBot`, `ClaudeBot`, `PerplexityBot`, `Perplexity-User`, `CCBot`, `Google-Extended`, `Applebot-Extended`, `Meta-ExternalAgent`, `Amazonbot`, `Bytespider`, `Diffbot`
- After main `User-agent: *` block, emit one block per agent:
  - `mode == "allow"` → `User-agent: <agent>\nAllow: /\n`
  - `mode == "disallow"` → `User-agent: <agent>\nDisallow: /\n`
  - `mode == "custom"` → for each agent in `ai_crawlers_custom`, emit `Allow: /`

**Tests — `tests/test_robots.cpp`** (new): allow mode emits all agents, disallow mode emits `Disallow: /`, custom mode emits only listed agents, default mode preserves current single-block behavior.

**Effort**: Small | **Dependencies**: None

---

### G2. `llms.txt` + `llms-full.txt` Generator   DONE

**Approach**: New module mirroring `rss.cpp`. `llms.txt` is a compact summary; `llms-full.txt` is the complete catalog with summaries. Emerging standard (~2024) for LLM crawlers.

**Config — `src/config/config.hpp`** (after `[modules]` block):
```cpp
// [modules.llms_txt]
bool        module_llms_txt       = false;
std::string llms_txt_description = "";  // site summary; defaults to site_description
int         llms_txt_max_pages   = 0;   // 0 = no cap for llms.txt; full list always complete
std::vector<std::string> llms_txt_exclude;  // glob patterns
```

**New files**:
- **`src/modules/llms_txt.hpp`** — `void generate_llms_txt(const Config&, const nlohmann::json& pages, const std::string& output_dir)`
- **`src/modules/llms_txt.cpp`**:
  - `llms.txt` format (per spec):
    ```
    # <Site Title>

    > <Site Description>

    ## Pages:
    - [Page Title](URL): Optional summary
    ...
    ```
  - Title from `cfg.site_title`. Description from `cfg.llms_txt_description` or fallback to a new `site_description` field.
  - Pages sorted by date descending. Each line: `- [{{ title }}]({{ base_url }}{{ url }}): {{ excerpt }}` (excerpt truncated to 160 chars).
  - `llms-full.txt` includes every non-excluded page; `llms.txt` honors `llms_txt_max_pages` (0 = all).
  - Exclusions reuse `glob_match()` logic — extract to `src/utils/glob.hpp` and share with `sitemap.cpp`.

**Builder wiring — `src/pipeline/builder.cpp`** (modules section, after `generate_json_feed`):
```cpp
if (cfg.module_llms_txt) {
    modules::generate_llms_txt(cfg, pages_array, cfg.output_dir);
}
```

**Tests — `tests/test_llms_txt.cpp`** (new): correct header format, page ordering, exclusion glob, max-pages truncation, missing description fallback.

**Effort**: Small-Medium | **Dependencies**: None

---

### G3. JSON-LD Structured Data Framework     DONE

**Approach**: The headline GEO feature. Expand `build_seo_meta()` to emit `<script type="application/ld+json">` blocks. Schema type auto-selected from `page.type` frontmatter (default `WebPage`); per-page override via `page.schema` frontmatter; multi-schema via `page.schema_extra` array. Multi-level: page schema + site-wide organization schema always emitted.

**Config — `src/config/config.hpp`** (new `[seo.json_ld]` block):
```cpp
// [seo.json_ld]
bool        json_ld_enabled    = true;   // emit JSON-LD by default
// [seo.organization] — emitted site-wide as Organization schema
std::string org_name;
std::string org_legal_name;
std::string org_logo;           // URL or path under static/
std::string org_founding_date;  // YYYY-MM-DD
std::vector<std::string> org_founders;     // names matching src/authors/<slug>.md
std::vector<std::string> org_same_as;      // [Wikidata, Wikipedia, GitHub, LinkedIn, ...]
std::string org_url;            // defaults to site_base_url
// [seo.website] — emitted site-wide as WebSite schema
std::string website_search_url_template;  // e.g. "/search?q={search_term_string}"
```

**Page frontmatter additions** (parsed in `src/content/frontmatter.cpp` known_keys):
- `type` — `Article | BlogPosting | NewsArticle | TechArticle | WebPage | Product | SoftwareApplication | FAQPage | HowTo | Recipe | Event | LocalBusiness | Person` (default `WebPage`; posts → `BlogPosting`)
- `schema` — JSON object overriding the auto-generated schema for this page
- `schema_extra` — array of additional schema objects to emit (e.g. BreadcrumbList, FAQPage on an article)
- `author` — resolves to a Person schema (see G6)
- `keywords` — array → `keywords` field
- `tldr`, `key_takeaways` — see G9

**Module — `src/modules/seo_schema.{hpp,cpp}`** (new):
```cpp
namespace cstatic::modules::seo_schema {
// Build the page-level JSON-LD object from page context + site config.
nlohmann::json build_page_schema(const Config& cfg, const nlohmann::json& page,
                                  const std::map<std::string,nlohmann::json>& authors_index);
// Build site-wide Organization + WebSite schemas.
nlohmann::json build_organization_schema(const Config& cfg);
nlohmann::json build_website_schema(const Config& cfg);
// Breadcrumb from a page's URL path.
nlohmann::json build_breadcrumb_schema(const Config& cfg, const nlohmann::json& page,
                                       const std::vector<nlohmann::json>& pages);
// Validate a schema object against its @type (basic field-presence checks; not full JSON Schema).
struct SchemaIssue { std::string field; std::string message; };
std::vector<SchemaIssue> validate(const nlohmann::json& schema);
}
```

Schema builders:
- **BlogPosting / Article**: `headline`, `datePublished`, `dateModified`, `author` (Person), `image` (from `page.og_image`), `publisher` (Organization), `mainEntityOfPage`, `description`, `wordCount` (see G12), `keywords`
- **WebPage** (default): `name`, `url`, `description`, `isPartOf` (WebSite)
- **Product**: maps frontmatter `price`, `currency`, `availability`, `brand`, `rating` to `Offer` + `AggregateRating`
- **SoftwareApplication**: `applicationCategory`, `operatingSystem`, `offers`, `aggregateRating`
- **Person**: derived from author pages (G6)
- **BreadcrumbList**: walks URL path segments, resolves each via `pages_array` titles
- **FAQPage**: built from G5 extraction
- **HowTo**: built from `{% schema "HowTo" %}` blocks (G4)

**Integration — `src/pipeline/builder.cpp`** at `build_seo_meta()` (~line 118):
1. After current OG/Twitter/canonical emission, append one `<script type="application/ld+json">` per schema.
2. Always emit `WebSite` schema (with `potentialAction: SearchAction` if `website_search_url_template` set).
3. Always emit `Organization` schema when `cfg.org_name` non-empty.
4. Emit page-level schema (type from frontmatter).
5. Emit `BreadcrumbList` for any page deeper than root.
6. Merge `page.schema_extra` verbatim.
7. JSON-LD serialized via `nlohmann::json::dump(2)` (auto-escaping).

**Validation**: `modules::seo_schema::validate()` runs in build; issues surface as `BuildError` with `Type::Warning` (non-fatal) so authors see missing fields but builds don't break.

**Tests — `tests/test_seo_schema.cpp`** (new):
- BlogPosting has all required fields
- Breadcrumb walks correctly
- Organization schema appears when configured
- `page.schema` overrides auto-generated fields
- `page.schema_extra` adds extra blocks
- Validation flags missing `headline` on Article
- Frontmatter type → schema type mapping

**Effort**: Large | **Dependencies**: None for core; G6 for Person/author resolution; G5 for FAQ; G4 for HowTo

---

### G4. Schema Blocks Shortcode   DONE

**Approach**: `{% schema "FAQPage" %}...{% endschema %}` emits **both** visible HTML and JSON-LD. Pairs with G3. Common cases: FAQ, HowTo, Q&A, Review.

**New files**:
- **`src/content/schema_blocks.{hpp,cpp}`** — runs in Phase 1b after shortcodes, before `render_markdown`. Parses `{% schema "Type" %}` blocks:
  ```cpp
  class SchemaBlockProcessor {
  public:
      SchemaBlockProcessor();
      // Returns transformed markdown (visible HTML blocks in place of schema tags).
      // Populates `out_schemas` with extracted JSON objects.
      std::string process(const std::string& markdown,
                          std::vector<nlohmann::json>& out_schemas,
                          const nlohmann::json& page_ctx) const;
  };
  ```

**Block syntax** (FAQPage example):
```
{% schema "FAQPage" %}
##? What is C-Static?
A fast C++ static site generator.

##? Is it free?
Yes, MIT licensed.
{% endschema %}
```

- FAQPage: `##? question` + following paragraphs = `Question`/`Answer` pairs. Visible HTML = `<section class="faq"><details><summary>…</summary>…</details>…</section>`.
- HowTo: `##! step title` + content = `HowToStep`. Visible HTML = `<ol class="howto">…</ol>`.
- Review: `{% schema "Review" item="Widget" rating="5" %}` → emits `Review` schema; visible HTML = `<div class="review" data-rating="5">…`.

Extracted schemas appended to `page.schema_extra` (see G3) — no separate render path needed.

**Builder wiring — `src/pipeline/builder.cpp`** Phase 1b (after shortcodes, before wikilinks):
```cpp
std::vector<nlohmann::json> page_schemas;
rp.parsed.body = schema_processor.process(rp.parsed.body, page_schemas, page_context_json);
// merge page_schemas into rp.parsed.frontmatter.schema_extra
```

**Scaffold**: add `shortcodes/schema-faq.html` and `shortcodes/schema-howto.html` examples (these are the visible-HTML templates).

**Tests — `tests/test_schema_blocks.cpp`** (new): FAQ extraction, HowTo step parsing, Review with attributes, unknown schema type → warning + passthrough, schemas flow into `page.schema_extra`.

**Effort**: Medium | **Dependencies**: G3

---

### G5. FAQ / Q&A Extraction   DONE

**Approach**: Auto-build `FAQPage` JSON-LD from any `##? question` blocks in markdown (even outside `{% schema %}` wrappers). Many AI engines weight Q&A blocks heavily.

**Wiring**: Reuse the `##?` parser from G4 (exposed as a standalone helper). In Phase 1b, scan for any `##? ` headings outside schema wrappers; collected Q&A pairs go into `page.faq` array which G3 emits as an additional `FAQPage` schema.

**Page context**: `page.faq` exposed to templates as `[{question, answer_html, answer_text}]` for custom rendering.

**Tests — extend `tests/test_schema_blocks.cpp`**: `##? ` outside schema block still produces FAQPage; mixed inline + schema FAQ merges.

**Effort**: Small (built on G4) | **Dependencies**: G3, G4

---

### G6. E-E-A-T Scaffolding (Authors + Organization) — DONE

**Approach**: First-class author entity system. `src/authors/<slug>.md` files have frontmatter: `name`, `title`, `bio`, `avatar`, `email`, `twitter`, `linkedin`, `github`, `website`, `same_as[]`, `expertise[]`. C-Static builds an authors index, exposes it to templates and to G3's Person schema.

**Config — `src/config/config.hpp`**:
```cpp
// [authors]
std::string authors_dir = "src/authors";  // or "authors" — single source of truth
bool        authors_enabled = false;
```

**New files**:
- **`src/content/authors_index.{hpp,cpp}`** — `AuthorsIndex` class:
  ```cpp
  class AuthorsIndex {
  public:
      void load(const std::string& authors_dir);
      bool has(const std::string& slug) const;
      nlohmann::json schema(const std::string& slug) const;  // Person JSON-LD
      nlohmann::json context(const std::string& slug) const; // for templates
      std::vector<std::string> all_slugs() const;
  };
  ```

**Builder wiring**:
1. Phase 1a: load authors index (single-threaded, before page parse loop).
2. For each page with `author: <slug>` frontmatter: resolve to Person schema; embed in page schema (`author` field).
3. Generate per-author pages at `/<authors_dir>/<slug>/` using `author.html` template (scaffold provides default). Each author page exposes `author.posts` (filtered `pages_array`).
4. Author page itself has `page.type = "ProfilePage"` → G3 emits `Person` schema as main entity.

**Frontmatter change**: `author` field added to known_keys in `frontmatter.cpp`. Resolves at render time; unknown slug → warning + raw string.

**Scaffold**: `archetypes/author.md`, `templates/author.html`, sample `src/authors/jane-doe.md`.

**Tests — `tests/test_authors_index.cpp`** (new): load + resolve, missing author warning, author page generation, Person schema shape, posts-list filter.

**Effort**: Medium | **Dependencies**: G3 (Person schema)

---

### G7. Citation Meta Tags   DONE

**Approach**: Emit Google Scholar / Perplexity / ChatGPT `citation_*` meta tags. Auto-derived from frontmatter.

**Tags** (added to `build_seo_meta` output):
- `citation_author` — one per author (resolved via G6 if `author` slug, else raw)
- `citation_title` — page title
- `citation_publication_date` — `page.date` as ISO 8601
- `citation_online_date` — `page.created` or `page.date`
- `citation_pdf_url` — `page.pdf_url` frontmatter (if provided)
- `citation_abstract` — `page.description` or `page.tldr` (G9)
- `citation_journal_title` — `page.journal` frontmatter
- `citation_doi` — `page.doi` frontmatter
- `citation_keywords` — semicolon-joined `page.tags`

**Frontmatter**: add `pdf_url`, `journal`, `doi` to known_keys.

**Tests — extend `tests/test_seo_schema.cpp`**: all citation tags present, multi-author emits multiple `citation_author`, missing fields omitted.

**Effort**: Small | **Dependencies**: G6 for author resolution

---

## Tier 2 — Content-Quality Signals

### G8. Passage Index — DONE

**Approach**: Per-page `page.passages` array of `{heading, text, id, level}` extracted from rendered HTML. Exposed to templates and emitted as JSON-LD extension (`hasPart`/`mainEntity` on the page schema). Helps AI engines locate citable passages.

**New files**:
- **`src/content/passage_index.{hpp,cpp}`** — `std::vector<Passage> extract_passages(const std::string& html_content)`:
  ```cpp
  struct Passage {
      std::string id;        // slugified heading text
      std::string heading;
      std::string text;      // text content until next heading (HTML stripped, capped at 500 chars)
      int level;             // 2, 3, ... (1 is page title, skipped)
  };
  ```
- Parser: walk rendered HTML (`rp.html_content`), regex `<h([2-6])[^>]*>(.*?)</h\1>`, extract following sibling text up to next heading. ID derived from heading (matches anchor ID from G11).

**Builder wiring — Phase 1b** (after `render_markdown`):
```cpp
rp.passages = extract_passages(rp.html_content);
page_context_json["passages"] = to_json_array(rp.passages);
```

**JSON-LD**: page schema gains `hasPart: [{@type: WebPageElement, name: heading, text: ...}]`.

**Tests — `tests/test_passage_index.cpp`**: extraction from typical article, heading hierarchy respected, ID generation stable, text truncation, code blocks ignored.

**Effort**: Small-Medium | **Dependencies**: None

---

### G9. TL;DR / Key Takeaways Frontmatter   DONE

**Approach**: `tldr` (string) and `key_takeaways` (array of strings) frontmatter flow into JSON-LD `description` (augmenting `excerpt`) and `mainEntity` of the page schema.

**Frontmatter**: add to known_keys. Render-time: `page.tldr`, `page.key_takeaways` available to templates.

**SEO**: `build_seo_meta` uses `tldr` (if present) as `meta description` (preferring concise over excerpt). Schema `description` prefers `tldr`, falls back to `excerpt`.

**Scaffold shortcode**: `shortcodes/tldr.html` for visible rendering:
```html
{{< tldr >}}A one-sentence summary.{{< /tldr >}}
{{< takeaways >}}
- Point one
- Point two
{{< /takeaways >}}
```
These shortcodes set `page.tldr`/`page.key_takeaways` in page context before render.

**Tests — extend `tests/test_seo_schema.cpp`**: TL;DR overrides meta description, key_takeaways populate schema, both optional.

**Effort**: Small | **Dependencies**: G3

---

### G10. Brand Mention Normalization   DONE

**Approach**: Single source of truth for organization identity, applied consistently across every page. Already partially specified in G3's `[seo.organization]` block — this feature completes it with a verification pass.

**Build-time check**: `modules::seo_schema::validate_organization()` runs once per build; warns if:
- `org_name` differs from `site_title` (intentional sometimes, but flagged)
- `org_logo` missing or file not found
- `same_as` entries malformed (not URLs)
- `org_founders` reference non-existent author slugs (G6)

**Template helper**: `{{ org }}` exposes the resolved Organization object (name, logo_url, same_as, founders) so authors can render consistent footers/cards.

**Tests — extend `tests/test_seo_schema.cpp`**: validation warnings fire correctly.

**Effort**: Small | **Dependencies**: G3, G6

---

### G11. Auto Table of Contents   DONE

**Approach**: Generate stable anchor IDs from headings and a `page.toc` tree. Pairs with G8 (passages share IDs). Improves on-page UX and Perplexity passage linking.

**New files**:
- **`src/content/toc.{hpp,cpp}`** — produces the toc tree AND post-processes HTML to inject IDs:
  ```cpp
  struct TocEntry { std::string id; std::string text; int level; std::vector<TocEntry> children; };
  std::vector<TocEntry> build_toc(std::string& html_content);  // mutates html to add id="..." on <h2-6>
  ```

**Builder wiring — Phase 1b** after `render_markdown`:
```cpp
auto toc = build_toc(rp.html_content);  // also adds IDs to headings
page_context_json["toc"] = to_json(toc);
```

**Scaffold shortcode**: `{{< toc >}}` renders the toc at a chosen location. Or template variable `{{ page.toc }}` for custom rendering.

**Tests — `tests/test_toc.cpp`**: ID generation from heading text, ID collision handling (suffix -1, -2), nested tree, skip levels (h2 → h4 produces correct nesting), existing IDs preserved.

**Effort**: Small-Medium | **Dependencies**: None (but IDs align with G8)

---

### G12. Reading Time / Word Count / Difficulty   DONE

**Approach**: Cheap computed fields exposed to templates and JSON-LD.

**New file**: `src/content/readability.{hpp,cpp}`:
```cpp
struct Readability { int word_count; int reading_time_min; std::string difficulty; };
Readability compute(const std::string& text);
```
- Word count: split on whitespace.
- Reading time: `ceil(word_count / 200)`.
- Difficulty: Flesch reading ease (implemented as ~30 lines of C++; syllable counter is heuristic).

**Wiring**: in Phase 1b after markdown render, compute on `rp.html_content` stripped of tags. Set `page.word_count`, `page.reading_time`, `page.difficulty`. JSON-LD `wordCount` and `timeRequired` (ISO 8601 duration `PT5M`).

**Tests — `tests/test_readability.cpp`**: counts on sample text, edge cases (code blocks, CJK), syllable heuristic sanity.

**Effort**: Small | **Dependencies**: None

---

## Tier 3 — Discovery & Verification

### G13. AI Sitemap Variants — DONE

**Approach**: Beyond the standard `sitemap.xml`, emit `sitemap-ai.xml` excluding thin pages (paginated indexes, taxonomy pages without prose, tag pages). Optionally include `image:` entries.

**Config — `src/config/config.hpp`**:
```cpp
// [modules.sitemap_ai]
bool        module_sitemap_ai     = false;
bool        sitemap_ai_include_images = true;
std::vector<std::string> sitemap_ai_exclude_types;  // page.type values to drop
```

**Module — extend `src/modules/sitemap.cpp`** (or new `sitemap_ai.cpp` reusing helpers):
- Filter `pages_array`: drop pages where `page.type ∈ sitemap_ai_exclude_types`, drop URLs matching `/tags/`, `/categories/`, `/page/N/`, anything without `word_count > 100` (G12).
- If `sitemap_ai_include_images`: emit `<image:image>` blocks for each image in `page.og_image` + any `image` frontmatter.

**Tests — extend `tests/test_build_integration.cpp`**: AI sitemap excludes taxonomy pages, image entries present.

**Effort**: Small-Medium | **Dependencies**: G12 (for word_count filter)

---

### G14. `.well-known/` AI Discovery   DONE

**Approach**: Generator for AI-plugin manifests and other discovery files. Reuses the existing module pattern.

**Config — `src/config/config.hpp`** (new `[well_known]` block):
```cpp
// [well_known.ai_plugin]
bool        wk_ai_plugin_enabled = false;
std::string wk_ai_plugin_schema_version = "1.0";
std::string wk_ai_plugin_name;          // defaults to site_title
std::string wk_ai_plugin_description;
// [well_known.security_txt] — already common; formalize
bool        wk_security_txt_enabled = false;
std::string wk_security_txt_content;    // raw, written verbatim
```

**New module**: `src/modules/well_known.{hpp,cpp}` — `void generate_well_known(const Config&, const std::string& output_dir)` writes files to `output/.well-known/`:
- `ai-plugin.json` — OpenAI plugin manifest (schema_v, name_for_human, description_for_human, api.url pointing at site, etc.)
- `security.txt` — written verbatim from config
- (future) `assetlinks.json`, `apple-app-site-association` — covered in Other plan

**Tests — `tests/test_well_known.cpp`**: ai-plugin.json valid JSON, security.txt written verbatim, disabled by default.

**Effort**: Small | **Dependencies**: None

---

### G15. Per-Page Markdown Mirror   DONE

**Approach**: Emit `<url>.md` alongside HTML for pages with `mirror_markdown: true` frontmatter or globally via config. Some crawlers (and many RAG pipelines) prefer raw markdown.

**Config — `src/config/config.hpp`**:
```cpp
// [build.markdown_mirror]
bool        markdown_mirror_enabled = false;
bool        markdown_mirror_all     = false;  // mirror every page; else opt-in via frontmatter
std::string markdown_mirror_suffix  = ".md";  // produces /posts/foo/index.md
```

**Wiring — Phase 3 output-write section** (after HTML write):
- If enabled and (`mirror_all` or page frontmatter `mirror_markdown: true`), write raw markdown body (with shortcodes/wikilinks resolved but not HTML-rendered) to the page's output dir + suffix.
- Add `<link rel="alternate" type="text/markdown" href="...">` to `<head>` via `build_seo_meta`.

**Frontmatter**: add `mirror_markdown` to known_keys.

**Tests — extend `tests/test_build_integration.cpp`**: mirror file created, alternate link emitted, frontmatter override works.

**Effort**: Small | **Dependencies**: None

---

### G16. `{% sources %}` Shortcode   DONE

**Approach**: Visible source list + JSON-LD `citation`/`isPartOf`. Pairs with G7.

**New file**: `shortcodes/sources.html` — the visible renderer. Behind the scenes, the shortcode processor extracts source URLs into `page.sources` and feeds G3 to emit `citation` entries on the page schema.

**Syntax**:
```
{% sources %}
- [Why X is better than Y](https://example.com/study) — Smith 2023
- https://doi.org/10.1000/xyz123
{% endsources %}
```

**Output**: visible `<ol class="sources">` + `schema_extra` push of `{@type: CreativeWork, citations: [...]}`.

**Tests — extend `tests/test_shortcodes.cpp`**: visible list rendered, citations flow into schema.

**Effort**: Small | **Dependencies**: G3

---

## Tier 4 — Measurement

### G17. `cstatic geo` Audit Subcommand   DONE

**Approach**: New CLI subcommand mirroring `cstatic check`. Scans built output, scores GEO readiness, lists remediation. Exits non-zero on hard issues (missing llms.txt when enabled).

**New files**:
- **`src/pipeline/geo_audit.{hpp,cpp}`** — `struct GeoAuditResult { int score; std::vector<GeoIssue> issues; }; GeoAuditResult audit_geo(const std::string& output_dir, const Config& cfg);`
- Checks:
  - **llms.txt present** (if `module_llms_txt` enabled) — else warn
  - **AI crawler allowlist** (any AI agent block in robots.txt)
  - **JSON-LD valid** on every HTML page (parse `<script type="application/ld+json">`, run `seo_schema::validate`)
  - **Organization schema** consistent across pages
  - **Author pages** exist for every `author` referenced
  - **FAQ blocks** present on at least N pages (informational)
  - **Citation tags** present on article-typed pages
  - **Passage index** populated on prose pages
  - **AI sitemap** present (if enabled)
- Score: 0-100 weighted by importance.

**CLI — `src/main.cpp`** (after `cmd_check`):
```cpp
auto* geo_cmd = app.add_subcommand("geo", "Audit Generative Engine Optimization readiness");
geo_cmd->callback([&]() { /* load config, run audit_geo, print report, exit */ });
```

**Report format**: per-issue `✓/✗/⚠` line + summary `GEO Score: 78/100`. Mirrors `format_build_error` style.

**Tests — `tests/test_geo_audit.cpp`**: each check fires correctly, score computation, exit codes.

**Effort**: Medium | **Dependencies**: G1, G2, G3, G6, G8

---

### G18. AI Referrer Tracking Snippets   DONE

**Approach**: Template helper emitting JS that detects AI referrers (Perplexity, ChatGPT, Bing Copilot, Google AI Overviews via specific query params) and logs to configured analytics as custom events.

**Config — `src/config/config.hpp`**:
```cpp
// [analytics.ai_referrers]
bool        ai_referrers_enabled = false;
std::string ai_referrers_provider;  // "plausible" | "umami" | "ga4" | "custom"
std::string ai_referrers_endpoint;  // for custom
```

**Template helper**: `{{ ai_referrer_snippet() }}` emits a `<script>` block (~1KB, no dependencies) that:
1. Checks `document.referrer` against known AI domains.
2. Checks URL params for AI sources (`?source=perplexity`, etc.).
3. Fires the configured analytics event.

**Scaffold**: included in `templates/default.html` when enabled.

**Tests — `tests/test_analytics_snippet.cpp`**: snippet contains expected referrer patterns, disabled config omits snippet.

**Effort**: Small | **Dependencies**: None

---

## Tier 5 — Experimental / Differentiating

> These items are higher-risk, higher-reward. They open new use cases beyond traditional SEO. Treat as research spikes before committing to API surface.

### G19. Model Context Protocol Server (`cstatic mcp`)

**Approach**: Expose site content as tools to Claude Desktop, Cursor, and other MCP-aware clients. Differentiator vs. every other SSG.

**New CLI subcommand**: `cstatic mcp` — runs stdio JSON-RPC server implementing MCP:
- Tools: `search_site(query)`, `read_page(url)`, `list_pages(tag?)`, `read_author(slug)`
- Resources: each page as a resource (markdown body)
- Backed by `search-index.json` (existing) + pages_array

**New files**:
- **`src/mcp/server.{hpp,cpp}`** — JSON-RPC dispatcher over stdin/stdout
- **`src/mcp/tools.{hpp,cpp}`** — tool implementations
- Reuses `search.cpp` index, `pages_array` builder

**Dependencies**: MCP spec is stable as of 2025; small JSON-RPC surface (~5 endpoints).

**Effort**: Medium-Large | **Dependencies**: G15 (for raw markdown resource serving)

---

### G20. Embeddings Index

**Approach**: `cstatic embed` builds a vector index of passages (G8), exposes `/search.json` for RAG-style retrieval. For sites that want to be the source AI answers cite.

**Config — `src/config/config.hpp`**:
```cpp
// [modules.embeddings]
bool        module_embeddings       = false;
std::string embeddings_provider     = "openai";  // or "local"
std::string embeddings_model        = "text-embedding-3-small";
std::string embeddings_api_key_env  = "OPENAI_API_KEY";
std::string embeddings_output       = "embeddings.json";
```

**New files**:
- **`src/modules/embeddings.{hpp,cpp}`** — calls provider API in batch (rate-limited), stores `{passage_id, url, heading, text, embedding[]}` to JSON. Embeddings computed at build time only (not per-request).
- **Client**: static `/search.json` + a small JS snippet (`static/js/semantic-search.js`) doing cosine similarity client-side for small sites, or pointing to a serverless function for larger ones.

**Caching**: `.cstatic_cache/embeddings.json` keyed by content hash; unchanged passages skip re-embedding.

**Tests — `tests/test_embeddings.cpp`**: batch construction, cache hit/miss, output format.

**Effort**: Medium-Large | **Dependencies**: G8 (passages), network access at build time

---

### G21. Named-Entity Graph

**Approach**: Auto-extract named entities from content (regex + gazetteer, no heavy NLP dep), emit as `definedTerm` JSON-LD and expose as a `/entities/` index. Helps brand/entity disambiguation.

**New files**:
- **`src/content/entity_graph.{hpp,cpp}`** — extracts proper nouns (capitalized sequences not at sentence start), known-entity gazetteer from `_data/entities.json`, organization names from G10's same_as list.
- Output: per-page `page.entities` array; site-wide `/entities/<slug>/` index pages cross-linking mentions.

**Tests — `tests/test_entity_graph.cpp`**: basic extraction, gazetteer matching, cross-linking.

**Effort**: Medium | **Dependencies**: G8

---

## Suggested Implementation Phasing

| Phase | Features | Rationale |
|-------|----------|-----------|
| **G-A: Foundations** | G1 (AI robots), G2 (llms.txt), G3 (JSON-LD framework) | Three foundational pieces; everything else composes on G3 |
| **G-B: Schema Content** | G4 (schema shortcode), G5 (FAQ extraction), G6 (authors), G7 (citations) | Authoring features that populate G3 schemas |
| **G-C: Quality Signals** | G8 (passages), G9 (TL;DR), G10 (brand normalization), G11 (TOC), G12 (readability) | Page-level quality outputs |
| **G-D: Discovery** | G13 (AI sitemap), G14 (.well-known), G15 (md mirror), G16 (sources shortcode) | Crawler/agent discovery |
| **G-E: Measurement** | G17 (`cstatic geo`), G18 (referrer tracking) | Close the loop |
| **G-F: Experimental** | G19 (MCP), G20 (embeddings), G21 (entity graph) | Research spikes |

Each phase can ship independently. Within a phase, items can be parallelized.

## Critical Files

| File | Features touching it |
|------|---------------------|
| `src/pipeline/builder.cpp` | G2, G3, G4, G5, G6, G7, G8, G9, G11, G12, G13, G15, G16, G20 |
| `src/config/config.hpp` + `.cpp` | G1, G2, G3, G6, G9, G13, G14, G15, G17, G18, G20 |
| `src/main.cpp` | G17, G18, G19 |
| `src/modules/robots.cpp` | G1 |
| `src/modules/seo_schema.{hpp,cpp}` (new) | G3, G4, G5, G6, G7, G9, G10, G16 |
| `src/content/frontmatter.cpp` | G3, G6, G7, G9, G15 |
| `CMakeLists.txt` + `tests/CMakeLists.txt` | All new source files |

## Verification

After each feature:
1. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCSTATIC_BUILD_TESTS=ON && cmake --build build -j$(sysctl -n hw.ncpu)`
2. `cd build && ctest --output-on-failure` — all tests must pass
3. Manual smoke test: `./build/cstatic init /tmp/test && ./build/cstatic build /tmp/test`
4. For GEO features specifically: validate JSON-LD output with [Schema.org Validator](https://validator.schema.org/) and Google's [Rich Results Test](https://search.google.com/test/rich-results)
5. After G17: `./build/cstatic geo /tmp/test` should report a sane score for a fresh scaffold
