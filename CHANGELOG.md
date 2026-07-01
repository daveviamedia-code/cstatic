# Changelog

All notable changes to C-Static are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
