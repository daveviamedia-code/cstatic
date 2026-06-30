# Changelog

All notable changes to C-Static are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
