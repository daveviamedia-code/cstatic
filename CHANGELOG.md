# Changelog

All notable changes to C-Static are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.19.0] - 2026-07-17

### Added
- `{% sources %}` shortcode (G16). Authors a visible numbered source list inline in markdown and emits matching Schema.org structured data so AI engines (Perplexity, ChatGPT, Google AI Overviews) and Google Scholar weight cited references. Each `{% sources %}...{% endsources %}` block accepts one entry per line — either a markdown link (`- [text](url)`, with an optional trailing annotation like ` — Smith 2023` that is kept in the visible HTML only) or a bare URL (`https://example.com/...`) which is autolinked with the URL as its text. The visible output is an `<ol class="sources">` with one `<li>` per entry; URLs are XML-escaped. The JSON-LD output (gated by the existing `seo.json_ld_enabled` flag from G3) is a `CreativeWork` with a `citations` array of `{@type:"CreativeWork", name, url}` objects — bare-URL entries omit `name` so the URL isn't duplicated. Each block emits its own CreativeWork; across multiple blocks, every recognized entry also accumulates into a `{{ page.sources }}` template variable of `{text, url, note}` objects for custom rendering. Pairs naturally with G7 (citation meta tags): G7 covers in-page citation metadata via frontmatter; G16 covers inline source lists authored in the body. No new config flag (pure syntax opt-in like G4/G5); the visible HTML always emits, the JSON-LD only emits when `json_ld_enabled` is on. Empty/unrecognized blocks fall back to a `warn:` and pass content through with no schema.

## [0.18.0] - 2026-07-16

### Added
- Per-page markdown mirror (G15). When `build.markdown_mirror.enabled = true`, C-Static emits a raw `<url>.md` file alongside the HTML so AI crawlers and RAG pipelines that prefer markdown can consume it directly. The mirror body contains the fully-processed markdown — shortcodes, schema blocks, standalone FAQ, and wikilinks resolved — but is NOT passed through the HTML renderer (so `# Heading` stays as `# Heading`, not `<h1>`). Mirror every page with `build.markdown_mirror.all = true`, or opt in individual pages with `mirror_markdown: true` frontmatter. The output filename is `index` + `build.markdown_mirror.suffix` (default `.md`, configurable — e.g. `.markdown`) sitting next to `index.html`. Mirrored pages also gain a `<link rel="alternate" type="text/markdown" href="<absolute-url>">` tag in `<head>` (resolved against `site.base_url`) advertising the mirror. Incremental builds preserve mirror files for cached pages and remove stale ones when a source is deleted — both the HTML orphan cleanup (which tracks active mirror paths) and the asset-pipeline orphan cleanup (which now skips files named exactly `index` + suffix, since they're builder-generated, not static assets; user-authored `.md` files are never touched). Opt-in and backwards-compatible; existing builds are unaffected unless the flag is set.

## [0.17.0] - 2026-07-15

### Added
- `.well-known/` AI discovery (G14). Two independently opt-in generators write to `output/.well-known/`: an OpenAI plugin manifest (`ai-plugin.json`) and a `security.txt`. Set `well_known.ai_plugin_enabled = true` to emit `ai-plugin.json` — `name`/`description` default to `site.title`/`site.description`, `name_for_model` is the slugified name, `logo_url` is resolved from `seo.org_logo` (if set) against the site base URL, `auth` is `none` (static sites have no authenticated API), and `api.url` points at the conventional `<base_url>/openapi.json` (drop a `static/openapi.json` if you want a live contract); `schema_version` defaults to `"v1"` (override via `well_known.ai_plugin_schema_version`). Set `well_known.security_txt_enabled = true` to write `security.txt` verbatim from `well_known.security_txt_content` (author supplies the full RFC 9116 text). When both are disabled (the default), no `.well-known/` directory is created. Opt-in and backwards-compatible; existing builds are unaffected unless a flag is set.

## [0.16.0] - 2026-07-14

### Added
- AI sitemap variants (G13). When `modules.sitemap_ai = true`, C-Static generates a curated `sitemap-ai.xml` alongside the standard `sitemap.xml`. This second sitemap filters out thin pages so AI crawlers (ChatGPT, Perplexity, Google AI Overviews) discover only substantive, citable prose content. Filtering rules (all must pass): inherits `sitemap.exclude` globs, drops URLs containing `/tags/`, `/categories/`, or `/page/` (taxonomy listings and paginated indexes), requires `word_count > 100` (from G12 readability — naturally excludes taxonomy pages which lack word_count), and drops pages whose `type` matches any value in `sitemap_ai.exclude_types`. When `sitemap_ai.include_images = true` (default), each `<url>` block gains deduped `<image:image>` entries collected from the page's `og_image` and `image` fields (relative URLs resolved to absolute via `site.base_url`); the `xmlns:image` namespace is only declared when at least one included page has images. Opt-in and backwards-compatible; existing builds are unaffected unless the flag is set.

## [0.15.0] - 2026-07-13

### Added
- Reading time / word count / difficulty (G12). Every page now exposes `{{ page.word_count }}`, `{{ page.reading_time }}` (minutes, assuming 200 words/minute), and `{{ page.difficulty }}` (`"easy"`, `"moderate"`, `"difficult"`, `"very-difficult"` via the Flesch reading-ease heuristic) — cheap computed fields derived from the rendered HTML. `<pre>` and `<code>` blocks are stripped before counting (code isn't prose), and CJK ideographs are counted as one word each since CJK text isn't whitespace-separated. Flesch difficulty is only computed for English-dominant prose (skipped when CJK characters dominate, since the formula is English-specific) and returns an empty string otherwise. When `seo.json_ld_enabled = true`, Article-typed pages (BlogPosting, Article, NewsArticle, TechArticle) also emit `wordCount` on the JSON-LD schema, and any page with a reading time emits `timeRequired` as an ISO 8601 duration (`PT5M` = 5 minutes). Explicit `page.schema.wordCount`/`page.schema.timeRequired` in frontmatter overrides the auto values via deep-merge. Always on (pure derived metadata, like `excerpt`, `passages`, and `toc`); JSON-LD emission rides the existing `json_ld_enabled` flag.

## [0.14.0] - 2026-07-10

### Added
- Auto Table of Contents (G11). C-Static now injects `id="..."` attributes into `<h2>`–`<h6>` heading tags (cmark-gfm doesn't emit them by default) and builds a `{{ page.toc }}` nested tree for template rendering. IDs use the same `utils::slugify` as G8 (passage index) so anchor links, passage `hasPart` URLs, and heading `id` attributes all stay in sync — `#introduction` resolves to `<h2 id="introduction">`. Duplicate headings get `-1`, `-2`, … suffixes (matching G8 exactly). Headings that already have an `id` attribute are preserved. Insert a `<!--toc-->` (or `<!-- toc -->`) marker anywhere in markdown content and C-Static replaces it with a rendered `<nav class="toc"><ul>…</ul></nav>` after the TOC tree is built. The tree handles skip-level nesting (h2 → h4 nests the h4 directly under the h2). Always on (pure derived metadata, like `excerpt`); no config flag needed.

## [0.13.0] - 2026-07-09

### Added
- Brand mention normalization (G10). When `seo.org_name` is set, C-Static now validates the Organization identity once per build and exposes a `{{ site.org }}` template variable so footers, author cards, and contact blocks all render from a single source of truth. Validation checks (non-fatal `warn:` on stderr): `org_name` diverging from `site_title` (informational — intentional when the organization and site have different names), `org_logo` pointing to a file that doesn't exist under `static_dir` (local paths only; absolute URLs are skipped), `org_same_as` entries that aren't valid URLs, and `org_founders` entries that don't match a known author slug (checked only when `authors.enabled = true` and the index is non-empty). The `{{ site.org }}` template object mirrors the JSON-LD Organization fields in a template-friendly shape: `name`, `url`, `legal_name`, `logo_url`, `founding_date`, `founders` (string array), `same_as` (string array) — only non-empty fields are included. No new config flag (validation is always on when `org_name` is set); backwards-compatible — existing builds without `org_name` are unaffected.

## [0.12.0] - 2026-07-08

### Added
- TL;DR / Key Takeaways frontmatter (G9). Two new frontmatter fields — `tldr` (string) and `key_takeaways` (array of strings) — flow into both SEO metadata and Schema.org JSON-LD. When `tldr` is present, it overrides `description`/`excerpt` as the meta description and schema `description` (priority: tldr → description → excerpt), giving AI engines and search results the most concise summary. When `key_takeaways` is a non-empty array, the page's JSON-LD schema gains a `mainEntity` `ItemList` of `ListItem` entries — each with `position` and `name` — so AI engines can surface the key points. Both fields are read from custom frontmatter (no breaking change to the frontmatter schema) and are available to templates as `{{ page.tldr }}` and `{{ page.key_takeaways }}`. Explicit `page.schema.mainEntity` in frontmatter overrides the auto-generated ItemList (deep-merge). Two new scaffold shortcodes — `{{< tldr >}}…{{< /tldr >}}` and `{{< takeaways >}}…{{< /takeaways >}}` — provide visible rendering wrappers. Always on (pure derived metadata, like `excerpt`); JSON-LD emission rides the existing `json_ld_enabled` flag.

## [0.11.0] - 2026-07-07

### Added
- Passage index (G8). Every page now exposes a `{{ page.passages }}` template array of `{id, heading, text, level}` entries extracted from `<h2>`–`<h6>` headings, and — when `seo.json_ld_enabled = true` — emits a `hasPart` array of `WebPageElement` entries on the page's JSON-LD schema so AI engines (Google AI Overviews, ChatGPT web search, Perplexity) can cite specific passages by anchor. Always on (pure derived metadata, like `excerpt`); JSON-LD emission rides the existing `json_ld_enabled` flag. Duplicate headings get `-1`, `-2`, … suffixes; passage text is capped at 500 characters. Also extracts a shared `utils::slugify` helper (header-only) from the wikilinks code so G11's auto-TOC anchor IDs will match passage IDs.

## [0.10.0] - 2026-07-06

### Added
- Citation meta tags (G7). When `seo.citation_tags_enabled = true`, pages emit `citation_*` meta tags (Google Scholar, Perplexity, ChatGPT): `citation_author` (one per author, resolved via the G6 authors index when available), `citation_title`, `citation_publication_date`, `citation_online_date` (from the `created` frontmatter field, falling back to `date`), `citation_pdf_url`, `citation_abstract` (prefers the `tldr` frontmatter field, falls back to `description`), `citation_journal_title`, `citation_doi`, and `citation_keywords` (tags semicolon-joined). The new frontmatter fields (`pdf_url`, `journal`, `doi`, `tldr`, `created`) are read from custom frontmatter — no breaking change to the frontmatter schema. Opt-in and backwards-compatible; existing builds are unaffected unless the flag is set.

## [0.9.0] - 2026-07-03

### Added
- E-E-A-T author entity system (G6). When `authors.enabled = true`, `.md` files under `authors.dir` (default `src/authors`) are loaded into an author index. Page frontmatter `author: <slug>` resolves to a full author object exposed to templates as `{{ page.author }}` (name, title, bio, avatar, social links, expertise) and emits a Schema.org `Person` JSON-LD object on the page. Each author gets a generated profile page at `/<authors_dir>/<slug>/` (rendered with `templates/author.html`) listing their published posts with a `Person` schema. Author files support `name`, `title`, `bio`, `avatar`, `email`, `twitter`, `linkedin`, `github`, `website`, `same_as` (array), and `expertise` (array). The full roster is available to every template as `{{ site.authors }}`. Opt-in and backwards-compatible — existing builds are unaffected unless the flag is set.

### Fixed
- Test-suite temp-directory isolation — fixtures across 9 test files named their temp dirs with unseeded `std::rand()` (or hard-coded names), so every test process produced the same path and parallel `ctest` runs collided on a shared `/tmp` dir. This was the root cause of the intermittent Ubuntu CI failures (a different integration test — `publish_future`, `markdown pagination`, etc. — flaking each run). A new shared `tests/test_util.hpp` provides `unique_temp_dir(prefix)` (nanosecond timestamp + atomic counter); all fixtures now use it. Test-only change — no binary impact.

## [0.8.3] - 2026-07-01

### Fixed
- CI test-suite reliability — `build.yml` now runs `ctest -E FileWatcher` on push. The FileWatcher cases need reliable kernel event delivery (kqueue/inotify) that is unreliable under ctest parallelism on shared runners, and their I/O load also perturbed the timing-sensitive `publish_future` integration test (which flaked on macOS once the FileWatcher test grew heavier). They remain covered by local runs. This reverts the v0.8.2 retry-loop change — the Ubuntu CI failure was not a watch-arm race but absent inotify delivery on the runner.

### Changed
- Release workflow no longer runs tests or builds Windows. `release.yml` drops its `Test` step (the quality gate lives in `build.yml`; decoupling prevents test flakiness from blocking binary publication) and drops the Windows matrix entry entirely. The native Windows build is deferred (Catch2 test-discovery runtime), and a perpetually-failing leg — blocking or not — was just noise on every tag; re-add it once unblocked. The release now publishes only the macOS + Linux binaries, which is all the Cloudflare deploy needs.

## [0.8.2] - 2026-07-01

### Fixed
- File-watcher test reliability on Linux CI — the callback-on-modification test case created a single probe file after a fixed 200ms sleep, which raced ahead of the inotify watch being armed on loaded single-vCPU runners and lost the `IN_CREATE` event. It now creates probe files in a bounded retry loop (up to the 3s deadline) so the test is robust to watcher-thread scheduling delay. Fixes the only failing Ubuntu test in CI.

### Changed
- Release pipeline no longer ships a fully-static Linux binary — the `release.yml` Ubuntu build drops `-DCSTATIC_STATIC=ON` and uses the default `-static-libgcc -static-libstdc++`, which links cleanly on the runner. The previous full `-static` failed (`ld: attempted static link of dynamic object 'libz.so'`) because the runner lacks `libz.a`. Full-static also broke DNS at runtime, and the Cloudflare deploy runs `cstatic build` locally, so the mostly-static binary is correct for the deploy use case.
- Windows release build is now non-blocking (`continue-on-error: ${{ matrix.experimental }}`). The Windows build is known-deferred (Catch2 test-discovery runtime), and previously its failure caused the `release` job (gated `needs: build`) to skip on every tag — so no binaries were ever published, even for the working Linux + macOS jobs. Linux and macOS releases now publish regardless of the Windows leg. `build.yml` likewise drops its now-redundant full-static Ubuntu job (the shipped Linux binary is no longer full-static).

## [0.8.1] - 2026-07-01

### Fixed
- Linux compilation — `libcmark-gfm-extensions_static` is now listed before `libcmark-gfm_static` at all three `target_link_libraries` sites. GNU `ld` resolves static archives strictly left-to-right, so the previous order (core before extensions) left `table.c`'s references to core symbols (`cmark_arena_push`/`cmark_arena_pop`) undefined on Ubuntu. Darwin resolves regardless of order, so macOS was unaffected. Also added the missing `#include <vector>` in `src/content/shortcodes.cpp` (it relied on a transitive include from `<filesystem>`/`<inja/inja.hpp>`, the fragile macOS-only pattern previously swept out of the rest of the tree). CI (`build.yml`) now runs Ubuntu (normal Release) and a full-static `-DCSTATIC_STATIC=ON` job alongside macOS, mirroring the `release.yml` matrix so the Cloudflare-deploy Linux binary links on every push.

## [0.8.0] - 2026-06-30

### Added
- Standalone FAQ extraction — `##? question` headings anywhere in a markdown body (not just inside a G4 `{% schema "FAQPage" %}` wrapper) now auto-build a `FAQPage` JSON-LD schema and render to the same collapsible `<section class="faq"><details><summary>…</summary>…</details>` visible HTML. The most common authoring case (a trailing FAQ section with no ceremony) is zero-config: just write `##?` headings. Each question also populates a new `{{ page.faq }}` template-context array (`[{question, answer_html, answer_text}]`) for custom sidebars/layouts. When a page has BOTH a G4 `{% schema "FAQPage" %}` block and trailing standalone `##?` questions, the two are merged into ONE `FAQPage` whose `mainEntity` holds every question (so AI engines see a single coherent Q&A document). The merged `FAQPage` rides the existing `schema_extra` channel that G3 emits verbatim when `[seo] json_ld_enabled = true`; visible HTML renders regardless. Answer-boundary semantics match G4 (an answer runs to the next `##?` or end of body), so standalone FAQ is terminal content — place it last on the page. No new config, no new CMake sources; reuses G4's `extract_faq_pairs` helper via the new `process_standalone_faq` + `merge_faq_into_schema_extra` API in `src/content/schema_blocks.{hpp,cpp}`. Pairs with G3/G4 for Google AI Overviews, Perplexity, and ChatGPT citation of Q&A content.

## [0.7.0] - 2026-06-30

### Added
- Schema blocks shortcode — `{% schema "Type" attrs %}...{% endschema %}` in markdown emits BOTH visible HTML and a JSON-LD schema object (appended to the page's `schema_extra`, so it is emitted verbatim by the G3 `seo_schema` module when `[seo] json_ld_enabled = true`). Supported types: **FAQPage** (`##? question` headings → `<section class="faq"><details><summary>…</summary>…</details>` plus a `FAQPage` schema with `Question`/`acceptedAnswer` pairs), **HowTo** (`##! step title` headings → `<ol class="howto"><li><h3>…</h3>…</li>` plus a `HowTo` schema with `HowToStep` entries), and **Review** (`item="…"`/`rating="N"` attrs + body → `<div class="review" data-rating="N">` plus a `Review` schema with `itemReviewed`/`reviewRating`/`reviewBody`). Answer/step/review markdown is rendered via the normal cmark-gfm pass, and plain-text variants feed the JSON-LD `text`/`reviewBody` fields. Unknown types produce a non-fatal `warn:` and pass the inner content through. The `##?` parser is exposed as the standalone `extract_faq_pairs()` helper for reuse by future FAQ extraction (G5). Pure syntax — no config flag; opt-in by writing a block. Pairs with G3 for AI Overview / Perplexity / ChatGPT citation of Q&A and how-to content.
- New content module `src/content/schema_blocks.{hpp,cpp}` with the `SchemaBlockProcessor` class and `extract_faq_pairs`/`FaqPair` helpers.

## [0.6.0] - 2026-06-30

### Added
- Schema.org JSON-LD structured data — `[seo] json_ld_enabled = true` makes `{{ seo_meta }}` append `<script type="application/ld+json">` blocks: a site-wide **WebSite** schema (always), a site-wide **Organization** schema when `seo.org_name` is set (`org_legal_name`/`org_logo`/`org_founding_date`/`org_founders`/`org_same_as`/`org_url`), a page-level schema whose `@type` auto-resolves from `page.schema["@type"]` → `page.type` → `BlogPosting` (URLs under `/posts/`) → `WebPage`, a **BreadcrumbList** for nested pages, and each `page.schema_extra` entry emitted verbatim. An explicit `page.schema` object is deep-merged over the auto-generated schema. Supported page `@type`s: `WebPage`, `BlogPosting`, `Article`, `NewsArticle`, `TechArticle`, `Product` (maps `brand`/`price`/`currency`/`availability`/`rating`/`reviewCount` → `Brand`/`offers`/`aggregateRating`), `SoftwareApplication` (maps `application_category`/`operating_system`). `seo.website_search_url_template` adds a WebSite `potentialAction` SearchAction. Missing required fields surface as non-fatal `warn:` lines on stderr. Default off preserves existing output — the keystone GEO feature for Google AI Overviews, ChatGPT, Perplexity, and Bing Copilot citation.
- New module `src/modules/seo_schema.{hpp,cpp}` (namespace `cstatic::modules::seo_schema`) with the `SchemaIssue` struct and `build_json_ld` / `build_website_script` / `build_organization_script` / `validate` API.

## [0.5.0] - 2026-06-29

### Added
- `llms.txt` generator — `[modules] llms_txt` writes `/llms.txt` (compact catalog) and `/llms-full.txt` (complete catalog) following the [llms.txt](https://llmstxt.org) spec for LLM crawlers (ChatGPT, Perplexity, Google AI Overviews, Claude). Options: `modules.llms_txt_description` (summary `>` line, falls back to a new `site.description` field), `modules.llms_txt_max_pages` (caps only `llms.txt`; `llms-full.txt` is always complete), and `modules.llms_txt_exclude` (glob patterns matched against page URLs). Pages are listed newest-first as `- [Title](base_url+url): excerpt` with excerpts truncated to 160 chars. Default off preserves existing output.
- Shared glob helper in `src/utils/glob.hpp` (`glob_match` + `matches_any_glob`), extracted from `sitemap.cpp` and now reused by both the sitemap and `llms.txt` modules.

## [0.4.0] - 2026-06-26

### Added
- AI crawler allowlist for `robots.txt` — `[modules] robots_ai_crawlers_mode` (`off` | `allow` | `disallow` | `custom`) emits per-crawler blocks for known AI/LLM crawlers (GPTBot, OAI-SearchBot, ClaudeBot, PerplexityBot, Perplexity-User, CCBot, Google-Extended, Applebot-Extended, Meta-ExternalAgent, Amazonbot, Bytespider, Diffbot) so AI search engines can crawl and cite your content. Default `off` preserves existing output.

### Fixed
- TOML config examples in `docs/config.md` and `skills/cstatic-configure-seo.md` used a `[modules]` header combined with dotted `modules.X` keys, which silently created a nested table and never applied. Examples moved to bare-key form.

## [0.3.0] - 2026-06-24

### Added
- OG image generation via headless Chrome or fallback SVG (`[og_images]` config)
- Shortcodes — reusable template fragments with `{{< name >}}` inline and `{{< block >}}...{{< /block >}}` block syntax
- `cstatic new` content scaffolder — generates markdown files from archetypes
- Scheduled publishing — `build.publish_future` config and frontmatter `date` field to hold posts for release
- `cstatic check` broken-link checker — post-build verifier for internal and external links
- Wikilinks and backlinks — `[[target]]` and `[[target|display]]` syntax with automatic reverse-link index
- Incremental dev server rebuilds — content-aware caching with `meta:pages_array` and `meta:wikilinks_index` hash invalidation
- Template inheritance — `{% extends "base" %}` and `{% block name %}` preprocessing with multi-level overrides
- JSON Feed output — `[modules] json_feed` config producing JSON Feed 1.1 (`feed.json`)
- `cstatic build --watch` mode — file-system watcher that rebuilds on content, template, or config changes
- Better error rendering — source context snippets, caret pointer under the error column, and `--verbose` phase timings
- Cloudflare Workers deploy scaffolding via `cstatic init` (wrangler.jsonc, GitHub Actions deploy workflow, .gitignore)
- AI agent surface — `AGENTS.md`, `llms.txt`, and `skills/` directory for AI-assisted development
- Template partials — `{% include "partial.html" %}` with `templates/partials/` convention
- HTML minification — whitespace collapse, comment removal, optional closing tag stripping (`[build.minify]` config)
- Content collections — `[[collection]]` TOML tables with default templates, URL patterns, and sort order
- Taxonomy pages — `[[taxonomy]]` tables for automatic tag and category index/term pages
- Image optimization — resize, JPEG quality, and WebP conversion via `[build.images]` config
- Asset fingerprinting — content-hashed filenames with `manifest.json` and `{{ asset() }}` template helper
- Syntax highlighting and markdown extensions — cmark-gfm tables, task lists, strikethrough, autolinks
- Build profiles — `--env production` flag with `config.<env>.toml` overlay and `{{ site.env }}` context
- Page aliases and redirects — frontmatter `aliases` array generating meta-refresh redirect pages
- Plugin hooks — `[hooks]` config for `before_build` and `after_build` scripts
- SEO helpers — `{{ seo_meta }}` context variable with Open Graph, Twitter Card, and canonical link tags
- Search index generation — `[build.search]` config producing `search-index.json` for Lunr.js, Pagefind, or Fuse.js
- Sitemap enhancements — frontmatter `sitemap_changefreq` and `sitemap_priority` fields
- `--drafts` flag for `build` and `serve` commands to include draft content
- Parallel page rendering with `--jobs` / `-j` flag
- Markdown pagination via `[[pagination]]` config tables
- Release workflow for macOS, Linux, and Windows binaries (triggered on `v*` tag push)
- Benchmarking infrastructure (`CSTATIC_BUILD_BENCHMARKS` CMake option)
- Comprehensive configuration documentation (`docs/config.md`)

### Changed
- Centralized duplicate file I/O and XML-escape utilities into `src/utils/file_io`
- Cached Inja environment and templates in `TemplateRenderer` for multi-threaded rendering
- Non-scalar frontmatter fields now support arrays and nested maps via `nlohmann::json`
- Cross-platform file watcher using kqueue (macOS), inotify (Linux), and ReadDirectoryChangesW (Windows)
- Windows-safe path handling via `std::filesystem::path` operations with forward-slash URL output
- JS minifier rewritten with a state-machine parser that correctly handles regex literals
- RSS feed now includes `<description>` elements with content excerpts

### Fixed
- CI build failures on Ubuntu and Windows — missing `<vector>`, `<cstdint>`, `<cstring>` includes; Windows `std::filesystem::path` to `std::string` conversions; portable current-working-directory handling
- Stale repository URL in release workflow

## [0.2.0] - 2026-06-10

Initial release.
