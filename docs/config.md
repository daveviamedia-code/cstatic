# Configuration Reference

All settings live in `config.toml` at the project root.

---

## `[site]` — Site Metadata

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `title` | string | — | Yes | Site title, available in templates as `{{ site.title }}` |
| `base_url` | string | — | Yes | Full URL (e.g. `"https://example.com"`). Trailing slash is stripped automatically. |
| `language` | string | `"en"` | No | Language code, used in `<html lang="{{ site.language }}">` |
| `twitter_handle` | string | — | No | Twitter/X handle (e.g. `"@username"`), used in `{{ seo_meta }}` twitter:site tag |
| `description` | string | — | No | Site summary; used as the `llms.txt` `>` line fallback when `modules.llms_txt_description` is unset |

```toml
[site]
title = "My Site"
base_url = "https://example.com"
language = "en"
twitter_handle = "@username"
description = "A concise summary of what this site is about."
```

---

## Environment / Build Profiles

C-Static supports per-environment configuration overlays. Pass `--env` (or `-e`) to `build` or `serve` to merge an environment-specific config on top of the base `config.toml`.

```bash
cstatic build --env production
cstatic serve -e staging
```

### Overlay File Naming

The overlay file is derived by inserting the environment name before the extension:

| Base config | `--env` | Overlay file |
|-------------|---------|-------------|
| `config.toml` | `production` | `config.production.toml` |
| `config.toml` | `staging` | `config.staging.toml` |

### Merge Semantics

- Tables (e.g. `[site]`, `[build]`) are **deep-merged** — nested keys from the overlay override individual values in the base.
- Scalars and arrays (including `[[collection]]`) are **replaced wholesale** — the overlay's value wins entirely.
- If the overlay file does not exist, a warning is printed and the base config is used unchanged.

```toml
# config.toml (base)
[site]
title = "My Site"
base_url = "https://example.com"
```

```toml
# config.production.toml (overlay)
[site]
base_url = "https://prod.example.com"
```

With `--env production`, the effective config has `title = "My Site"` (from base) and `base_url = "https://prod.example.com"` (overridden).

### `{{ site.env }}` Template Variable

The current environment name is available in all templates. When no `--env` is passed, it defaults to `"development"`:

```html
{% if site.env == "production" %}
  <script src="{{ asset("js/analytics.js") }}"></script>
{% endif %}
```

---

## `[build]` — Build Paths

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `source_dir` | string | `"src"` | Directory containing Markdown content files |
| `output_dir` | string | `"output"` | Directory where generated HTML is written |
| `template_dir` | string | `"templates"` | Directory containing Inja layout templates |
| `static_dir` | string | `"static"` | Directory containing static assets (CSS, JS, images) |

```toml
[build]
source_dir = "src"
output_dir = "output"
template_dir = "templates"
static_dir = "static"
```

---

## `[build.incremental]` — Incremental Builds

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `enabled` | bool | `true` | When enabled, only pages whose content hash has changed are rebuilt |
| `hash_file` | string | `".cstatic_cache/hashes.json"` | Path to the hash cache file (relative to project root) |

```toml
[build.incremental]
enabled = true
hash_file = ".cstatic_cache/hashes.json"
```

Use `cstatic build --full` to force a clean rebuild regardless of this setting.

### Build flags

| Flag | Description |
|------|-------------|
| `--full` | Force a clean rebuild (ignore the hash cache) |
| `--drafts` | Include draft pages in the output |
| `-j, --jobs <N>` | Parallel render threads (0 = auto) |
| `-e, --env <name>` | Build environment overlay (e.g. `production`) |
| `-v, --verbose` | Print per-phase build timing (parse, render, data pages, assets, modules) to stderr |

---

## `[build.minify]` — Asset Minification

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `css` | bool | `true` | Minify CSS files from the static directory |
| `js` | bool | `true` | Minify JavaScript files from the static directory |
| `html` | bool | `true` | Minify generated HTML output |

```toml
[build.minify]
css = true
js = true
html = true
```

---

## `[build.search]` — Client-Side Search Index

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `enabled` | bool | `false` | Generate a JSON search index at build time |
| `output` | string | `"search-index.json"` | Output filename (written to the output directory root) |

When enabled, a `search-index.json` file is generated in the output directory containing an array of all published pages with their `title`, `url`, `excerpt`, `date`, and `tags`. This can be consumed by client-side search libraries (Lunr.js, Fuse.js, etc.).

```toml
[build.search]
enabled = true
output = "search-index.json"
```

---

## `[build.images]` — Image Optimization

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `optimize` | bool | `false` | Enable image processing (resize + recompress) |
| `max_width` | int | `1920` | Resize images wider than this (pixels) |
| `quality` | int | `85` | JPEG quality 1–100 (higher = better quality, larger files) |
| `webp` | bool | `false` | Generate WebP copies via `cwebp` (if available on PATH) |
| `avif` | bool | `false` | Generate AVIF copies via `avifenc` (if available on PATH) |

```toml
[build.images]
optimize = true
max_width = 1920
quality = 85
webp = true
avif = false
```

### Supported Formats

Images: `.jpg`, `.jpeg`, `.png`, `.gif`, `.bmp`, `.tga` — resized and recompressed via stb.
SVG files are passed through as-is (stb doesn't handle vector images).

### External Tools

WebP and AVIF conversion require external tools installed on your system:

- **WebP**: Install `cwebp` (e.g. `brew install webp` on macOS, `apt install webp` on Ubuntu)
- **AVIF**: Install `avifenc` (e.g. `brew install libavif` on macOS, `apt install libavif-bin` on Ubuntu)

If a tool is not found, C-Static prints a one-time notice and skips that conversion.

---

## `build.fingerprint_assets` — Cache Busting

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `fingerprint_assets` | bool | `false` | Add content-based hashes to asset filenames for cache-busting |

```toml
[build]
fingerprint_assets = true
```

When enabled, assets are written with content hashes in their filenames:

- `css/style.css` → `css/style.a3f7b2c1.css`
- `js/app.js` → `js/app.e5d4f3a2.js`

A `manifest.json` file is generated in the output directory mapping original paths to fingerprinted paths:

```json
{
  "css/style.css": "css/style.a3f7b2c1.css",
  "js/app.js": "js/app.e5d4f3a2.js"
}
```

Both fingerprinted and original files are written (for backwards compat during development).

### Template Usage

Use the `{{ asset() }}` function in templates to resolve fingerprinted paths:

```html
<link rel="stylesheet" href="{{ asset("css/style.css") }}">
<script src="{{ asset("js/app.js") }}"></script>
```

When fingerprinting is off, `{{ asset() }}` returns the original path unchanged.

---

## `build.publish_future` — Scheduled Publishing

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `publish_future` | bool | `false` | When `false`, pages with a frontmatter `date` set in the future are skipped during build |

```toml
[build]
publish_future = false
```

Pages dated in the future are treated like drafts — they are excluded from the build (and from collections, feeds, and sitemaps) until their date arrives. This lets you stage content ahead of time and have it go live automatically on later builds.

- Dates are parsed as `YYYY-MM-DD` in local time. Malformed dates are ignored (the page builds normally).
- The `--drafts` flag (and the dev server) bypasses scheduling so you can preview upcoming content locally.
- Skipped pages are reported in the build summary as `(N scheduled)`.

Set `publish_future = true` to publish everything regardless of date (useful for previews or imports).

---

## `[build.highlight]` — Syntax Highlighting

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `enabled` | bool | `true` | Apply syntax highlighting to fenced code blocks |
| `style` | string | `"github"` | CSS theme name: `"github"` (light) or `"github-dark"` |

```toml
[build.highlight]
enabled = true
style = "github"
```

When enabled, C-Static post-processes `<pre><code class="language-X">` blocks, wrapping tokens in `<span class="hl-*">` elements, and writes `output/css/highlight.css`. Reference it in your templates:

```html
<link rel="stylesheet" href="/css/highlight.css">
```

### Supported Languages

JavaScript/TypeScript (`js`, `javascript`, `ts`), Python (`py`, `python`), C/C++ (`c`, `cpp`, `c++`), Go (`go`), Rust (`rs`, `rust`), Bash/Shell (`sh`, `bash`, `shell`), JSON (`json`), YAML (`yaml`, `yml`), HTML (`html`, `xml`), and CSS (`css`, `scss`). Code blocks without a language hint or with an unsupported language are left unhighlighted.

### Token Classes

| Class | Meaning |
|-------|---------|
| `hl-keyword` | Language keywords (`var`, `def`, `if`, ...) |
| `hl-string` | String literals |
| `hl-comment` | Comments (line and block) |
| `hl-number` | Numeric literals |
| `hl-function` | Function names (identifier followed by `(`) |
| `hl-built_in` | Built-in types and constants |
| `hl-attr` | Property/attribute names (CSS, HTML) |

---

## `[build.markdown]` — Markdown Extensions

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `extensions` | string[] | all enabled | Subset of GFM extensions to enable |
| `shortcodes_dir` | string | `"shortcodes"` | Directory containing shortcode templates (relative to project root) |
| `wikilinks` | bool | `false` | Rewrite `[[target]]` / `[[target\|display]]` syntax to `<a>` and expose `page.backlinks` |

C-Static uses [cmark-gfm](https://github.com/github/cmark-gfm) for rendering, which supports GitHub-Flavored Markdown extensions. By default all four are enabled. To enable only a subset:

```toml
[build.markdown]
extensions = ["table", "tasklist"]
```

### Available Extensions

| Extension | Syntax | Output |
|-----------|--------|--------|
| `table` | Pipe tables | `<table>` elements |
| `tasklist` | `- [x]` / `- [ ]` | Checkboxes in list items |
| `strikethrough` | `~~text~~` | `<del>text</del>` |
| `autolink` | Bare URLs | `<a>` tags auto-linked |

### Shortcodes

Shortcodes are reusable content components invoked from markdown via `{{< name params >}}`. They expand to HTML **before** the cmark-gfm render pass, so the emitted HTML passes through (`CMARK_OPT_UNSAFE` is enabled). Templates live in the `shortcodes_dir` (default `shortcodes/`) as `<name>.html` files rendered with [Inja](https://github.com/pantor/inja).

**Inline shortcodes** take positional args (`{{ params.0 }}`, `{{ params.1 }}`) and named args (`{{ named.src }}`):

```markdown
{{< youtube dQw4w9WgXcQ >}}
{{< figure src="/img/cat.jpg" alt="A cat" caption="Mittens" >}}
```

**Block shortcodes** wrap inner content (available as `{{ content }}`). With no params, the opener is treated as a block start; a matching `{{< /name >}}` closes it. Same-name blocks nest correctly:

```markdown
{{< note >}}
This is **important** text rendered inside the shortcode.
{{< /note >}}
```

The render context exposes `params` (positional array), `named` (object), `content` (block inner HTML), and `page` (current page's `title`, `url`, `slug`, `date`). An unknown shortcode prints a notice on stderr and expands to nothing. Shortcodes are disabled automatically when the directory is empty or missing.

### Schema Blocks

Schema blocks emit **both** visible HTML and a JSON-LD schema object from one markdown construct. They use `{% schema "Type" attrs %}...{% endschema %}` tags (distinct from `{{< >}}` shortcodes) and run after shortcodes, before `[[wikilinks]]` and the cmark-gfm render — so the emitted raw HTML survives into the final document (`CMARK_OPT_UNSAFE`). Each extracted schema is appended to the page's `schema_extra` and is emitted verbatim as an additional `<script type="application/ld+json">` block by the SEO module when `[seo] json_ld_enabled = true` (see [JSON-LD Options](#json-ld-options)). No config flag — opt in by writing a block; a body without schema blocks is a no-op pass-through.

**FAQPage** — `##? question` headings start each Q/A; the paragraphs that follow are the answer (rendered as HTML in a `<details><summary>`; plain text feeds the JSON-LD `acceptedAnswer.text`):

```markdown
{% schema "FAQPage" %}
##? What is C-Static?
A fast C++ static site generator.

##? Is it free?
Yes, MIT licensed.
{% endschema %}
```

Visible: `<section class="faq"><details><summary>…</summary>…</details>…</section>`. Style via the `.faq` CSS class.

**HowTo** — `##! step title` headings start each step; following content is the step text:

```markdown
{% schema "HowTo" %}
##! Install it
Run `make`.

##! Run it
Type `./cstatic`.
{% endschema %}
```

Visible: `<ol class="howto"><li><h3>…</h3>…</li>…</ol>`.

**Review** — `item="…"` and `rating="N"` attributes on the opener; the block body is the review text:

```markdown
{% schema "Review" item="Widget" rating="5" %}
This widget is great.
{% endschema %}
```

Visible: `<div class="review" data-rating="5">…</div>`. `rating` is optional (omits `data-rating` and `reviewRating`).

Unknown types print a non-fatal `warn:` on stderr and pass the inner content through without emitting a schema. A `FAQPage`/`HowTo` block with no `##?`/`##!` markers behaves the same way. Blocks do not nest.

#### Standalone `##?` FAQ extraction

`##? question` headings **outside** any `{% schema %}` wrapper are also auto-processed: each renders the same `<section class="faq"><details><summary>…</summary>…</details>` visible HTML as a `FAQPage` schema block, and a `FAQPage` JSON-LD object is merged into `schema_extra` (emitted verbatim when `[seo] json_ld_enabled = true`). This is the common authoring case — a trailing FAQ section needs no wrapper ceremony:

```markdown
# Pricing

Plans start at $0…

##? Is there a free tier?
Yes — the Hobby plan is free forever.

##? Do you offer refunds?
Full refund within 30 days, no questions asked.
```

Each question also populates a `{{ page.faq }}` array (`[{question, answer_html, answer_text}]`) for custom sidebars or JSON exports; the inline body HTML always renders regardless of templates.

If a page has **both** a `{% schema "FAQPage" %}` block and trailing standalone `##?` questions, the two are merged into ONE `FAQPage` whose `mainEntity` holds every question — AI engines see a single coherent Q&A document. Answer-boundary semantics match the schema block (an answer runs to the next `##?` or end of body), so standalone FAQ is **terminal content** — place it last on the page. No config flag (pure opt-in syntax).

### Wikilinks & Backlinks

Wikilinks provide a slug-or-title shorthand for cross-referencing pages, and the build automatically exposes the reverse relationship (`page.backlinks`) to templates. Enable with:

```toml
[build.markdown]
wikilinks = true
```

**Syntax** — `[[target]]` or `[[target|display]]` in markdown is rewritten to `<a href="...">display</a>` **before** the cmark-gfm render pass, so the emitted HTML passes through (`CMARK_OPT_UNSAFE` is enabled):

```markdown
See [[hello]] and [[Hello World|the hello post]] for context.
```

**Resolution order** — the target string is matched against the index of every parsed page in this order:

1. Exact filename stem (e.g. `[[hello]]` matches `src/hello.md` → `/hello/`).
2. Slugified target against stem/slug index (e.g. `[[Hello World]]` → `hello-world`).
3. Lowercased target against the lowercase-title index.
4. Exact match against frontmatter `aliases` (see below).

When no match is found, the wikilink renders as `<a class="wikilink-unresolved">display</a>` (no `href`) and a warning is printed to stderr. Self-links do not appear in the target's backlinks.

**Aliases** — frontmatter `aliases` (a string array) let old URLs or alternate names resolve to the current page:

```markdown
---
title: New Title
aliases = ["/old-path/", "legacy-name"]
---
```

**Backlinks** — every page's render context gains `page.backlinks`, a JSON array of `{ "url", "title" }` pages that wikilink to it. Render with an [Inja](https://github.com/pantor/inja) loop:

```html
{% if page.backlinks.size() > 0 %}
<aside>
  <h2>Pages linking here</h2>
  <ul>{% for bl in page.backlinks %}<li><a href="{{ bl.url }}">{{ bl.title }}</a></li>{% endfor %}</ul>
</aside>
{% endif %}
```

**Incremental cache** — any change to the resolver index (titles, aliases, or stems) invalidates every page on the next incremental build, so backlink sections stay consistent.

**Known limitations** — data-driven pages aren't indexed (they're generated after Phase 1). Resolution is exact-match only; there is no fuzzy matching.

---

## `[build.markdown_mirror]` — Per-Page Markdown Mirror (G15)

Emit a raw `<url>.md` file alongside each page's HTML so AI crawlers and RAG pipelines that prefer markdown can consume it directly. The mirror body holds the fully-processed markdown — shortcodes, schema blocks, standalone `##?` FAQ, and wikilinks all resolved — but is **not** passed through the HTML renderer, so a `# Heading` stays as `# Heading` rather than `<h1>`. Each mirrored page also gains a `<link rel="alternate" type="text/markdown" href="…">` tag in `<head>` (resolved against `site.base_url`) advertising the mirror.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `enabled` | bool | `false` | Turn the mirror feature on |
| `all` | bool | `false` | Mirror every page; otherwise pages opt in via `mirror_markdown: true` frontmatter |
| `suffix` | string | `".md"` | Output filename suffix — produces `<dir>/index` + suffix next to `index.html` (e.g. `.markdown`) |

```toml
[build.markdown_mirror]
enabled = true
all = false       # opt-in per page (set true to mirror every page)
suffix = ".md"
```

Per-page opt-in:

```markdown
---
title: My Post
mirror_markdown: true
---
```

**Orphan cleanup** — incremental builds remove stale mirror files (only files named exactly `index` + suffix, the form C-Static generates). User-authored `.md` files copied into `output/` via `static/` are never touched.

---

## `[og_images]` — Open Graph Image Generation

Generate a social-card image per page by rendering an Inja SVG template, then (optionally) converting it to PNG. The generated image URL is injected into the page's `{{ seo_meta }}` `og:image` tag (when the page has no explicit `image` frontmatter), into `sitemap.xml`, and into the RSS feed.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `enabled` | bool | `false` | Generate OG images for every page with a non-empty title |
| `template` | string | `"og-default"` | SVG template name (without `.svg`), loaded from the template directory |
| `output_format` | string | `"png"` | Output format: `"png"` (requires a converter) or `"svg"` |
| `width` | int | `1200` | PNG width in pixels (used by the converter) |
| `height` | int | `630` | PNG height in pixels (used by the converter) |
| `output_dir` | string | `"og"` | Subdirectory under `output/` where images are written |

```toml
[og_images]
enabled = true
template = "og-default"
output_format = "png"
width = 1200
height = 630
output_dir = "og"
```

### How It Works

1. For each page with a non-empty `title`, C-Static renders `templates/<template>.svg` with Inja, receiving `{{ page.title }}`, `{{ page.date }}`, `{{ page.excerpt }}`, `{{ page.url }}`, `{{ site.title }}`, and `{{ site.base_url }}`.
2. The rendered SVG is always written to `output/<output_dir>/<slug>.svg`.
3. When `output_format = "png"` and a converter is available, a PNG is written alongside it and the `og:image` URL points to the `.png`. Otherwise the SVG is used directly.
4. Text values are XML-escaped so the SVG stays well-formed.

The `<slug>` is derived from the page URL: leading/trailing slashes are stripped, internal slashes become hyphens (e.g. `/posts/hello/` → `posts-hello`; `/` → `index`).

### SVG→PNG Converters

PNG output requires one of these tools on your `PATH` (checked in this order):

- **rsvg-convert** — `brew install librsvg` (macOS) / `apt install librsvg2-bin` (Ubuntu)
- **convert** (ImageMagick) — `brew install imagemagick` / `apt install imagemagick`
- **inkscape** — `brew install --cask inkscape` / `apt install inkscape`

If none is found, C-Static prints a one-time notice and writes SVG images instead (which work as `og:image` on most platforms).

### Customizing the Template

A default `templates/og-default.svg` (1200×630) is scaffolded by `cstatic init`. SVG `<text>` does not wrap automatically — for long titles, use multiple `<tspan>` lines or shorten titles in frontmatter.

---

## `[modules]` — Built-in Modules

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `sitemap` | bool | `true` | Generate `sitemap.xml` with all published pages |
| `rss` | bool | `false` | Generate RSS feed at `feed.xml` |
| `json_feed` | bool | `false` | Generate JSON Feed 1.1 at `feed.json` |
| `robots` | bool | `false` | Generate `robots.txt` |
| `llms_txt` | bool | `false` | Generate `llms.txt` + `llms-full.txt` for LLM crawlers |
| `sitemap_ai` | bool | `false` | Generate curated `sitemap-ai.xml` for AI crawlers |

```toml
[modules]
sitemap = true
rss = false
json_feed = false
robots = false
llms_txt = false
sitemap_ai = false
```

### RSS Options

When `modules.rss = true`, these keys customize the feed:

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `modules.rss_title` | string | `site.title` | RSS feed title (defaults to site title) |
| `modules.rss_description` | string | `""` | RSS feed description |
| `modules.rss_item_count` | int | `20` | Maximum number of items in the feed |

```toml
[modules]
rss = true
modules.rss_title = "My Site Feed"
modules.rss_description = "Latest posts from My Site"
modules.rss_item_count = 10
```

### JSON Feed Options

When `modules.json_feed = true`, a [JSON Feed 1.1](https://jsonfeed.org/version/1.1)
file is generated at `<output_dir>/<json_feed_output>` (default `feed.json`).
Each item carries the page's rendered HTML body as `content_html`, plus
`title`, `url`, `summary`, and `date_published`.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `modules.json_feed_output` | string | `"feed.json"` | Output filename (written under `output_dir`) |

The feed reuses the RSS options for shared metadata — `modules.rss_title`,
`modules.rss_description`, and `modules.rss_item_count` — so RSS and JSON
Feed stay in sync when both are enabled:

```toml
[modules]
json_feed = true
modules.rss_title = "My Site Feed"
modules.rss_description = "Latest posts from My Site"
modules.rss_item_count = 10
modules.json_feed_output = "feed.json"
```

### Robots.txt Options

When `modules.robots = true`, these keys customize `robots.txt`:

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `modules.robots_user_agent` | string | `"*"` | User-agent directive value |
| `modules.robots_include_sitemap` | bool | `true` | Automatically add a `Sitemap:` line pointing to `sitemap.xml` |
| `modules.robots_disallow` | string[] | `[]` | List of `Disallow` paths |
| `modules.robots_ai_crawlers_mode` | string | `"off"` | AI/LLM crawler handling: `"off"` (no AI blocks), `"allow"` (`Allow: /` each known agent), `"disallow"` (`Disallow: /` each), or `"custom"` (allow only `robots_ai_crawlers_custom`) |
| `modules.robots_ai_crawlers_custom` | string[] | `[]` | Agent names to allow when mode is `"custom"` (e.g. `["GPTBot", "ClaudeBot"]`) |

Known AI agents (used by `allow`/`disallow`): `GPTBot`, `OAI-SearchBot`, `ClaudeBot`, `PerplexityBot`, `Perplexity-User`, `CCBot`, `Google-Extended`, `Applebot-Extended`, `Meta-ExternalAgent`, `Amazonbot`, `Bytespider`, `Diffbot`.

```toml
[modules]
robots = true
robots_user_agent = "*"
robots_include_sitemap = true
robots_disallow = ["/admin/", "/private/"]
# Welcome AI/LLM crawlers so your content can be cited by AI search:
robots_ai_crawlers_mode = "allow"
# ...or allow only specific agents:
# robots_ai_crawlers_mode = "custom"
# robots_ai_crawlers_custom = ["GPTBot", "ClaudeBot", "PerplexityBot"]
```

### llms.txt Options

When `modules.llms_txt = true`, C-Static generates two files following the
[llms.txt spec](https://llmstxt.org), an emerging convention for LLM crawlers:

- `llms.txt` — compact catalog, honors `llms_txt_max_pages` (0 = no cap)
- `llms-full.txt` — every non-excluded page, never capped

Pages are listed newest-first (the same order used by RSS/JSON Feed). Each
entry is `- [Title](<base_url><url>): excerpt`, with the excerpt truncated to
160 characters and the `: excerpt` portion omitted when the page has none.
Pages with empty URL/title or matching an `llms_txt_exclude` glob are dropped.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `modules.llms_txt_description` | string | `""` | Site summary emitted as the `>` line; falls back to `site.description` |
| `modules.llms_txt_max_pages` | int | `0` | Max pages in `llms.txt` (0 = no cap); `llms-full.txt` is always complete |
| `modules.llms_txt_exclude` | string[] | `[]` | Glob patterns matched against page URLs (e.g. `["/tags/*"]`) |

```toml
[site]
description = "A concise summary of what this site is about."

[modules]
llms_txt = true
modules.llms_txt_description = "Override summary for LLM crawlers."  # optional
modules.llms_txt_max_pages = 50        # optional; 0 = no cap
modules.llms_txt_exclude = ["/tags/*", "/page/*"]
```

### JSON-LD Options

When `seo.json_ld_enabled = true`, C-Static emits Schema.org
[JSON-LD](https://schema.org) `<script type="application/ld+json">` blocks
into `{{ seo_meta }}` on every page. This is the single biggest lever for
Generative Engine Optimization (GEO) — Google AI Overviews, ChatGPT,
Perplexity, and Bing Copilot all weight structured data heavily when citing
sources.

Each page receives, in order:

1. A site-wide **WebSite** schema (always).
2. A site-wide **Organization** schema when `seo.org_name` is set.
3. A page-level schema whose `@type` is auto-selected (see below).
4. A **BreadcrumbList** for nested pages (URLs deeper than `/`).
5. One verbatim block per `schema_extra` frontmatter entry.

A page-level **`hasPart`** array is auto-attached to the page schema when
the page contains headings (see [Passage Index](#passage-index) below).

**Type resolution** (first match wins): `page.schema["@type"]` → `page.type`
frontmatter → `BlogPosting` if the URL starts with `/posts/` → `WebPage`.
Supported `@type`s: `WebPage`, `BlogPosting`, `Article`, `NewsArticle`,
`TechArticle`, `Product`, `SoftwareApplication`. Any other value is emitted
as-is with `WebPage`-style field mapping.

An explicit `page.schema` object is **deep-merged** over the auto-generated
schema, so you override individual fields without losing the auto-filled
`headline`, `datePublished`, `author`, `image`, etc. Missing required fields
are surfaced as non-fatal `warn:` lines on stderr.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `seo.json_ld_enabled` | bool | `false` | Emit JSON-LD structured-data blocks (opt-in; off preserves existing output) |
| `seo.org_name` | string | `""` | Organization name; setting it enables the site-wide Organization schema |
| `seo.org_legal_name` | string | `""` | Legal name (emitted as `legalName`) |
| `seo.org_logo` | string | `""` | Logo URL (relative `/`-paths prefixed with `base_url`; local paths are validated against `static_dir` — see Brand Mention Normalization below) |
| `seo.org_founding_date` | string | `""` | `foundingDate` (e.g. `"2015-01-01"`) |
| `seo.org_founders` | string[] | `[]` | Each becomes `{@type:Person, name}` under `founder` |
| `seo.org_same_as` | string[] | `[]` | `sameAs` array of profile URLs |
| `seo.org_url` | string | `site.base_url` | Organization URL (defaults to site base URL) |
| `seo.website_search_url_template` | string | `""` | When set, adds a WebSite `potentialAction` SearchAction; use `{search_term_string}` as the placeholder |

```toml
[seo]
json_ld_enabled = true
org_name = "Acme Inc"
org_logo = "/logo.png"
org_founding_date = "2015-01-01"
org_founders = ["Alice", "Bob"]
org_same_as = ["https://twitter.com/acme", "https://github.com/acme"]
website_search_url_template = "/search?q={search_term_string}"
```

### Brand Mention Normalization

When `seo.org_name` is set, C-Static validates the Organization identity once
per build (non-fatal `warn:` on stderr) and exposes a `{{ site.org }}` template
variable so every footer, contact card, and about block renders from a single
source of truth.

**Validation checks** (always on when `org_name` is non-empty):

| Check | Warning condition |
|-------|-------------------|
| `org_name` vs `site_title` | Diverges from `site_title` (informational — intentional when the organization and site have different names) |
| `org_logo` | Local path (not an absolute URL) whose file doesn't exist under `static_dir` |
| `org_same_as[N]` | Entry doesn't look like a URL (no `://`) |
| `org_founders` | Entry doesn't match a known author slug (checked only when `authors.enabled = true` and the index is non-empty) |

**Template variable** — `{{ site.org }}` provides a template-friendly object
mirroring the JSON-LD fields: `name`, `url`, `legal_name`, `logo_url` (resolved
against `base_url`), `founding_date`, `founders` (string array), `same_as`
(string array). Only non-empty fields are included. The object is empty when
`org_name` is unset.

```html
<footer>
  {% if site.org.name %}
  <p>{{ site.org.name }} — founded {{ site.org.founding_date }}</p>
  {% for link in site.org.same_as %}
  <a href="{{ link }}">{{ link }}</a>
  {% endfor %}
  {% endif %}
</footer>
```

### Passage Index

C-Static automatically extracts a **passage index** from every page's rendered
HTML — an array of `{id, heading, text, level}` for each `<h2>`–`<h6>` heading
plus the sibling text that follows it (up to the next heading). This is pure
derived metadata (like `excerpt`), so it is always on regardless of config.

AI engines (Google AI Overviews, ChatGPT web search, Perplexity) increasingly
cite **specific passages** rather than whole pages. Exposing passages both to
templates and as machine-readable JSON-LD gives those engines the boundaries
and anchor targets they need.

**Template usage** — `{{ page.passages }}` is an array of objects:

```html
{% for p in page.passages %}
<li><a href="#{{ p.id }}">{{ p.heading }}</a> <small>(L{{ p.level }})</small></li>
{% endfor %}
```

Each entry has:

| Field | Description |
|-------|-------------|
| `id` | Slugified heading text (e.g. `"getting-started"`); collisions get `-1`, `-2`, … suffixes |
| `heading` | Heading text with HTML stripped |
| `text` | Body text until the next heading, HTML stripped, whitespace collapsed, capped at 500 chars |
| `level` | Heading level (`2`–`6`; `<h1>` is skipped as the page title) |

**JSON-LD `hasPart`** — when `seo.json_ld_enabled = true`, each passage is
emitted as a `WebPageElement` under the page schema's `hasPart`:

```json
"hasPart": [
  {
    "@type": "WebPageElement",
    "name": "Getting Started",
    "text": "Install with cmake …",
    "url": "https://example.com/docs/#getting-started"
  }
]
```

The `url` is `<canonical or base_url + page.url>#<passage-id>` — ready for AI
engines to cite the specific section. An explicit `page.schema.hasPart` in
frontmatter overrides the auto-generated array (deep-merge semantics).

> **Note**: cmark-gfm doesn't emit `id="…"` attributes on heading tags by
> default. G11 (auto TOC, see below) injects matching `id` attributes so
> in-page anchor links resolve — they share the same slugify algorithm.

### Auto Table of Contents

C-Static automatically builds a **table of contents** from every page's
`<h2>`–`<h6>` headings and injects `id="..."` attributes into those tags so
`#anchor` links resolve in browsers (cmark-gfm doesn't emit IDs by default).
This is pure derived metadata (like `excerpt`), so it is always on regardless
of config.

**`{{ page.toc }}`** — A nested tree of `{id, text, level, children}` entries.
Each entry corresponds to one heading; `children` contains sub-headings nested
beneath it. `id` is the slugified heading text (collisions get `-1`, `-2`, …
suffixes, matching G8 passage IDs exactly). Use this in templates for custom
TOC rendering:

```html
{% if page.toc %}
<nav class="toc">
  <ul>
  {% for entry in page.toc %}
    <li class="level-{{ entry.level }}"><a href="#{{ entry.id }}">{{ entry.text }}</a></li>
  {% endfor %}
  </ul>
</nav>
{% endif %}
```

**`<!--toc-->` marker** — Insert `<!--toc-->` (or `<!-- toc -->` with spaces)
anywhere in your markdown content and C-Static replaces it with a rendered
`<nav class="toc"><ul>…</ul></nav>` after the TOC tree is built. The rendered
nav uses `<li class="toc-level-N">` entries with nested `<ul>`s for child
headings.

Headings that already have an `id` attribute (e.g. from raw HTML in your
markdown) are preserved verbatim — no duplicate ID is injected.

### Reading Time / Word Count / Difficulty

C-Static computes three cheap readability metrics from every page's rendered
HTML and exposes them as template variables. This is pure derived metadata
(like `excerpt`, `passages`, and `toc`), so it is always on regardless of
config.

**Template variables** — All three are available on every `page` context:

| Variable | Type | Notes |
|----------|------|-------|
| `{{ page.word_count }}` | int | Whitespace-separated words + CJK ideographs (each CJK char counts as one word, since CJK text isn't whitespace-separated). |
| `{{ page.reading_time }}` | int | Estimated reading time in minutes, computed as `ceil(word_count / 200)` (200 = average adult reading speed for English prose). |
| `{{ page.difficulty }}` | string | One of `"easy"`, `"moderate"`, `"difficult"`, `"very-difficult"` via the Flesch reading-ease heuristic, or empty string when not computable. |

`<pre>` and `<code>` blocks are stripped before counting — code isn't prose
and skews the syllable counter. The Flesch formula is English-specific, so
`difficulty` is only computed for English-dominant prose and returns an empty
string when CJK characters make up half or more of the counted words.

**JSON-LD** — When `[seo] json_ld_enabled = true` (see [JSON-LD Options](#json-ld-options)),
Article-typed pages (BlogPosting, Article, NewsArticle, TechArticle) also
emit `wordCount` on the page schema, and any page with a reading time emits
`timeRequired` as an ISO 8601 duration (`PT5M` = 5 minutes). Explicit
`page.schema.wordCount` or `page.schema.timeRequired` in frontmatter overrides
the auto values via deep-merge.

### Citation Tags

When `seo.citation_tags_enabled = true`, C-Static emits `citation_*` meta tags
into `{{ seo_meta }}` on every page. These tags are consumed by Google Scholar,
Perplexity, ChatGPT web search, and other AI-powered research tools for
citation and source attribution.

| Tag | Source | Notes |
|-----|--------|-------|
| `citation_author` | `author` frontmatter | One tag per author; resolved via the authors index (G6) when available |
| `citation_title` | `title` | |
| `citation_publication_date` | `date` | ISO 8601 (`YYYY-MM-DD`) |
| `citation_online_date` | `created` frontmatter, falls back to `date` | |
| `citation_pdf_url` | `pdf_url` frontmatter | |
| `citation_abstract` | `tldr` frontmatter, falls back to `description` | |
| `citation_journal_title` | `journal` frontmatter | |
| `citation_doi` | `doi` frontmatter | |
| `citation_keywords` | `tags` | Semicolon-joined |

Missing fields are simply omitted — no partial or empty tags are emitted.

```toml
[seo]
citation_tags_enabled = true
```

### TL;DR / Key Takeaways

Two frontmatter fields — `tldr` (string) and `key_takeaways` (array of strings) —
flow into both SEO metadata and Schema.org JSON-LD. Both are read from custom
frontmatter, so no config flag is required.

**`tldr`** — When present, overrides `description` and `excerpt` as:
- The `<meta name="description">` tag (priority: tldr → description → excerpt)
- The JSON-LD schema `description` field (same priority chain)

This gives AI engines and search results the most concise page summary.

**`key_takeaways`** — When a non-empty array, the page's JSON-LD schema gains a
`mainEntity` `ItemList`:

```json
"mainEntity": {
  "@type": "ItemList",
  "itemListElement": [
    {"@type": "ListItem", "position": 1, "name": "First point"},
    {"@type": "ListItem", "position": 2, "name": "Second point"}
  ]
}
```

An explicit `page.schema.mainEntity` in frontmatter overrides the auto-generated
ItemList (deep-merge semantics). JSON-LD emission requires `seo.json_ld_enabled
= true`; the `{{ page.tldr }}` and `{{ page.key_takeaways }}` template variables
are always available.

**Frontmatter example:**

```yaml
---
title: "My Article"
tldr: "A one-sentence summary of the key insight."
key_takeaways:
  - Point one
  - Point two
---
```

**Scaffold shortcodes** — `shortcodes/tldr.html` and `shortcodes/takeaways.html`
provide visible rendering wrappers:

```markdown
{{< tldr >}}A one-sentence summary.{{< /tldr >}}

{{< takeaways >}}
- Point one
- Point two
{{< /takeaways >}}
```

### Authors Options

When `authors.enabled = true`, C-Static loads `.md` files from `authors.dir` (default `src/authors`) into an author index. Each file's stem is the **slug** referenced by page frontmatter `author: <slug>`. The filename `src/authors/jane-doe.md` → slug `jane-doe`.

Pages that set `author: jane-doe` get:
- `{{ page.author }}` in templates — a full author object (`name`, `title`, `bio`, `avatar`, `email`, `twitter`, `linkedin`, `github`, `website`, `same_as`, `expertise`, `url`).
- A resolved Schema.org `Person` JSON-LD object on the page (when `seo.json_ld_enabled = true`).

Each loaded author also gets a generated profile page at `/<authors_dir_basename>/<slug>/` (e.g. `/authors/jane-doe/`), rendered with `templates/author.html`. The template receives `{{ author }}` (the author object plus a `posts` array of their published pages) and `{{ page }}` with `type = "ProfilePage"`. The full roster is available to every template as `{{ site.authors }}` (a `{slug: object}` map).

Author frontmatter fields:

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Display name (falls back to the filename stem if omitted) |
| `title` | string | Job title / role → `jobTitle` in Person schema |
| `bio` | string | Short biography |
| `avatar` | string | Avatar image URL → `image` in Person schema |
| `email` | string | Email → `email` in Person schema |
| `twitter` / `linkedin` / `github` | string | Social profiles (available in templates) |
| `website` | string | Primary URL → `@id` in Person schema |
| `same_as` | string[] | Additional profile URLs → `sameAs` array |
| `expertise` | string[] | Topics (available in templates) |

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `authors.enabled` | bool | `false` | Enable the author entity system (opt-in) |
| `authors.dir` | string | `"src/authors"` | Directory containing author `.md` files |

```toml
[authors]
enabled = true
dir = "src/authors"
```

**Per-page frontmatter** (lives in `custom`, available as `page.*` in templates):

| Field | Used by | Description |
|-------|---------|-------------|
| `type` | all | Schema `@type` override (e.g. `"Product"`, `"SoftwareApplication"`). |
| `author` | articles | String slug or name. When `authors.enabled = true` and the slug matches an author file, resolves to a full `Person` schema (see [Authors Options](#authors-options)). Otherwise emitted as `{@type:Person, name}`. |
| `schema` | all | Object deep-merged over the auto-generated schema. Use `schema["@type"]` to override the type itself. |
| `schema_extra` | all | Array (or single object) emitted verbatim as additional `<script>` blocks. |
| `keywords` | all | Array or comma string; falls back to comma-joined `tags` for articles. |
| `brand`, `price`, `currency`, `availability`, `rating`, `reviewCount` | Product / SoftwareApplication | Commerce fields → `brand`, `offers`, `aggregateRating`. |
| `application_category` (or `category`), `operating_system` | SoftwareApplication | Mapped to `applicationCategory` / `operatingSystem`. |

---

## `[sitemap]` — Sitemap Options

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `exclude` | string[] | `[]` | URL paths to exclude from the sitemap |

```toml
[sitemap]
exclude = ["/404.html", "/private/"]
```

### AI Sitemap Options

When `modules.sitemap_ai = true`, C-Static generates a curated
`sitemap-ai.xml` alongside the standard `sitemap.xml`. This second sitemap
filters out thin pages (taxonomy listings, paginated indexes, low word-count
pages) so AI crawlers (ChatGPT, Perplexity, Google AI Overviews) discover
only substantive, citable prose content.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `include_images` | bool | `true` | Embed `<image:image>` entries from `og_image`/`image` fields |
| `exclude_types` | string[] | `[]` | `page.type` values to exclude (e.g. `["landing"]`) |

```toml
[modules]
sitemap_ai = true

[sitemap_ai]
include_images = true
exclude_types = ["landing"]
```

**Filtering rules** (all must pass for a page to appear in `sitemap-ai.xml`):

1. **Inherits `sitemap.exclude`** — pages matching those globs are dropped.
2. **Thin URL patterns** — URLs containing `/tags/`, `/categories/`, or
   `/page/` are always excluded (taxonomy listings and paginated indexes).
3. **Word count gate** — `word_count` (from G12 readability) must be `> 100`.
   Pages without `word_count` (e.g. data-driven pages) are excluded.
4. **Type exclusion** — if `page.type` matches any value in `exclude_types`.

When `include_images = true`, each `<url>` block gains `<image:image>` entries
collected from the page's `og_image` and `image` fields (deduped, relative
URLs resolved to absolute via `site.base_url`). The `xmlns:image` namespace
is only declared when at least one included page has images.

---

## `[well_known]` — `.well-known/` Discovery Files

Two independently opt-in generators write standard discovery files to
`output/.well-known/`. When both are disabled (the default), no
`.well-known/` directory is created.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `ai_plugin_enabled` | bool | `false` | Emit `.well-known/ai-plugin.json` (OpenAI plugin manifest) |
| `ai_plugin_schema_version` | string | `"v1"` | Manifest `schema_version` field |
| `ai_plugin_name` | string | `site.title` | Display name (`name_for_human`) |
| `ai_plugin_description` | string | `site.description` | Description (`description_for_human`/`_for_model`) |
| `security_txt_enabled` | bool | `false` | Emit `.well-known/security.txt` (RFC 9116) |
| `security_txt_content` | string | `""` | Raw security.txt content, written verbatim |

```toml
[well_known]
ai_plugin_enabled = true
ai_plugin_schema_version = "v1"
# ai_plugin_name = "Acme Blog"        # defaults to site.title
# ai_plugin_description = "..."        # defaults to site.description

security_txt_enabled = true
security_txt_content = "Contact: mailto:security@example.com\nExpires: 2026-12-31T23:59:59.000Z\n"
```

**`ai-plugin.json` field derivation**:
- `schema_version` — from `ai_plugin_schema_version` (default `"v1"`).
- `name_for_human` — `ai_plugin_name`, falling back to `site.title`.
- `name_for_model` — the slugified name (lowercase, hyphenated).
- `description_for_human` / `description_for_model` — `ai_plugin_description`,
  falling back to `site.description`. Omitted entirely when both are empty.
- `auth` — `{"type": "none"}` (static sites have no authenticated API surface).
- `api` — `{"type": "openapi", "url": "<base_url>/openapi.json"}`. Drop a
  `static/openapi.json` if you want a live API contract.
- `logo_url` — resolved from `seo.org_logo` (if set) against the site base URL;
  absolute URLs kept as-is. Omitted when `org_logo` is unset.

**`security.txt`** is written verbatim from `security_txt_content` — author
supplies the full RFC 9116 text (`Contact:`, `Expires:`, etc.).

---

## `[hooks]` — Build Hooks

Run custom shell scripts before and/or after each build. Hooks receive environment variables with build context.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `before_build` | string | `""` | Shell script to run before the build starts |
| `after_build` | string | `""` | Shell script to run after the build completes |

```toml
[hooks]
before_build = "./scripts/pre-build.sh"
after_build = "./scripts/post-build.sh"
```

### Environment Variables

The following environment variables are set before each hook runs:

| Variable | Description |
|----------|-------------|
| `CSTATIC_ENV` | The build environment name (e.g. `development`, `production`) |
| `CSTATIC_OUTPUT_DIR` | The output directory path (e.g. `output`) |
| `CSTATIC_PAGES_BUILT` | Number of pages built (always `0` for `before_build`) |

### Hook Behavior

- If a hook script file does not exist, a warning is printed and the build continues.
- If `before_build` exits with a non-zero code, the build is **aborted immediately** with an error.
- If `after_build` exits with a non-zero code, the error is collected but the build output is still written.
- Hooks are executed via the system shell (`std::system`).

### Example: Run a pre-build script

```bash
#!/bin/bash
# scripts/pre-build.sh
echo "Building site..."
# Generate dynamic content, fetch data, etc.
```

```bash
chmod +x scripts/pre-build.sh
```

---

## Error Reporting

When a page fails to build (e.g. a template error, a frontmatter parse error, or a markdown rendering error), C-Static **collects all errors** instead of aborting on the first one. Valid pages continue building; only broken pages are skipped.

After the build completes, errors are printed as a numbered summary to stderr:

```
error: Build failed with 2 error(s):

  1. src/posts/hello.md: template 'post' (templates/post.html:5)
    Variable 'undefined_var' not found for json data

  2. src/about.md: markdown error
    Unexpected token in markdown
```

For template errors with a known line number, ±3 lines of source context are shown with a `>` marker on the failing line.

---

## `[data]` — Data Files

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `data_dir` | string | `"_data"` | Directory containing JSON or YAML data files |

```toml
[data]
data_dir = "_data"
```

## `[[data_source]]` — Data-Driven Pages

Array of tables defining data-driven page generation. Each entry maps a data file to a template.

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `file` | string | — | Yes | Data filename relative to `data_dir` (e.g. `"products.json"`) |
| `template` | string | — | Yes | Template name (without `.html` extension) used to render pages |
| `url_pattern` | string | `""` | No | URL pattern with `{{ variable }}` interpolation (e.g. `"/products/{{ slug }}/"`) |
| `item_key` | string | `"slug"` | No | Field in each data item used for URL interpolation |
| `per_page` | int | `0` | No | Number of items per page (`0` = no pagination) |
| `per_item` | bool | `false` | No | Generate a separate page for each data item |

```toml
[[data_source]]
file = "products.json"
template = "product"
url_pattern = "/products/{{ slug }}/"
item_key = "slug"
per_page = 10
per_item = true
```

When `per_page > 0`, pagination context is available in templates as `{{ pagination.page }}`, `{{ pagination.total_pages }}`, `{{ pagination.prev_url }}`, `{{ pagination.next_url }}`, and `{{ pagination.items }}`.

When `per_item = true`, each item gets its own page with the item available as `{{ item }}`.

---

## `[[collection]]` — Content Collections

Array of tables defining content types (e.g., "posts", "projects") with their own default templates, URL patterns, sort order, and automatically-generated index pages.

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `name` | string | — | Yes | Collection name — matches files in `src/<name>/` directory |
| `template` | string | — | Yes | Default template for collection items (without `.html` extension) |
| `index_template` | string | `<name>-index` | No | Template for the auto-generated collection index page |
| `url_pattern` | string | `""` | No | URL pattern with `{{ slug }}` interpolation (e.g. `"/blog/{{ slug }}/"`). Empty = auto from file path |
| `sort_by` | string | `"date"` | No | Frontmatter field to sort collection pages by |
| `sort_order` | string | `"desc"` | No | Sort direction: `"desc"` (newest first) or `"asc"` (oldest first) |

```toml
[[collection]]
name = "posts"
template = "post"
index_template = "posts-index"
sort_by = "date"
sort_order = "desc"

[[collection]]
name = "projects"
template = "project"
url_pattern = "/work/{{ slug }}/"
sort_by = "date"
sort_order = "desc"
```

### How Collections Work

1. Any markdown file under `src/<collection.name>/` is automatically associated with that collection
2. If the page has no explicit `layout` in frontmatter, the collection's `template` is used
3. If the collection has a `url_pattern`, output URLs are derived from it (using the filename stem as `slug`)
4. An index page is generated at `/<collection.name>/index.html` using the `index_template`
5. The `{{ collections }}` variable is available in all templates, keyed by collection name

### Collection Template Context

**Index page** (`index_template`) receives:

| Variable | Description |
|----------|-------------|
| `collection.name` | Collection name (e.g. `"posts"`) |
| `collection.pages` | Array of page objects, sorted by `sort_by` / `sort_order` |
| `site` | Site config |
| `pages` | All site pages |

**Any page** receives the `collections` variable:

```
{{ collections.posts }}    → array of all posts, sorted
{{ collections.projects }} → array of all projects, sorted
```

---

## `[[taxonomy]]` — Taxonomy Pages

Array of tables defining automatic index pages for tags, categories, or any frontmatter field.

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `key` | string | — | Yes | Frontmatter field to index (e.g. `"tags"`, `"category"`) |
| `template` | string | — | Yes | Template for term listing pages (without `.html` extension) |
| `index_template` | string | same as `key` | No | Template for the taxonomy index page |

```toml
[[taxonomy]]
key = "tags"
template = "tag"
index_template = "tags"

[[taxonomy]]
key = "category"
template = "category"
index_template = "categories"
```

### How Taxonomies Work

1. For each taxonomy, the builder extracts the frontmatter field (e.g. `tags`) from every page
2. Two types of pages are generated per taxonomy:
   - **Index page** at `/<key>/` (e.g. `/tags/`) — lists all terms with counts
   - **Term pages** at `/<key>/<term>/` (e.g. `/tags/webdev/`) — lists all pages with that term
3. Term pages are included in the sitemap

### Taxonomy Template Context

**Term page** (`template`) receives:

| Variable | Description |
|----------|-------------|
| `taxonomy.key` | Taxonomy key (e.g. `"tags"`) |
| `taxonomy.term` | Current term (e.g. `"webdev"`) |
| `taxonomy.pages` | Array of page objects with this term |
| `site` | Site config |
| `pages` | All site pages |

**Index page** (`index_template`) receives:

| Variable | Description |
|----------|-------------|
| `taxonomy.key` | Taxonomy key (e.g. `"tags"`) |
| `taxonomy.terms` | Array of `{term, count, url}` objects |
| `site` | Site config |
| `pages` | All site pages |

---

## Frontmatter: Aliases

The `aliases` frontmatter field generates HTML redirect pages at the specified URLs, pointing to the current page. This is useful when moving or renaming content — old URLs redirect to the new location automatically.

```markdown
---
title: My Post
date: 2024-01-15
aliases: [/old-url/, /2023/my-post/]
---

Content here.
```

### How Aliases Work

1. For each alias URL, C-Static generates a redirect HTML page containing a `<meta http-equiv="refresh">` tag and a `<link rel="canonical">` pointing to the destination page.
2. Alias pages are **included in `sitemap.xml`** so search engines pick up the redirects.
3. Alias pages do **not** appear in `{{ pages }}` or `{{ collection.pages }}` during template rendering — they are redirect stubs, not content pages.
4. Redirect HTML is minified when `build.minify.html` is enabled.

### Alias URL → Output Path Rules

| Alias URL | Output file |
|-----------|------------|
| `/old-url/` | `old-url/index.html` |
| `/another/old/path/` | `another/old/path/index.html` |
| `/old.html` | `old.html` (extension preserved) |
| `/` | `index.html` |

---

## Frontmatter: SEO & Sitemap Fields

These optional frontmatter fields enrich the generated `<meta>` tags and sitemap entries for each page.

| Field | Description |
|-------|-------------|
| `description` | SEO description text. Falls back to the auto-generated excerpt (first 200 chars of content) if omitted. |
| `image` | Open Graph image URL. Relative paths starting with `/` are prefixed with `site.base_url`. |
| `canonical` | Canonical URL override. Defaults to `site.base_url + page.url`. |
| `sitemap_changefreq` | Sitemap `<changefreq>` value (e.g. `"monthly"`, `"weekly"`). |
| `sitemap_priority` | Sitemap `<priority>` value (e.g. `"0.8"`). Accepts numeric or string. |
| `mirror_markdown` | When `build.markdown_mirror.enabled = true` and `all = false`, set to `true` to emit a `<url>.md` mirror for this page (see [Markdown Mirror](#buildmarkdown_mirror--per-page-markdown-mirror-g15)). |

```markdown
---
title: My Post
description: A concise summary for search results and social cards.
image: /images/og-cover.png
canonical: https://example.com/custom-canonical-url
sitemap_changefreq: monthly
sitemap_priority: 0.8
---
```

### `{{ seo_meta }}` Template Variable

When you include `{{ seo_meta }}` in a template (typically inside `<head>`), C-Static generates the following tags automatically:

- `<meta name="description">` (if description/excerpt is non-empty)
- `<meta property="og:title">`, `og:description`, `og:url`, `og:image`
- `<meta name="twitter:card">` (`summary_large_image` if image present, else `summary`)
- `<meta name="twitter:site">` (if `site.twitter_handle` is set)
- `<link rel="canonical">`
- `<link rel="alternate" type="text/markdown">` (only on mirrored pages — see [Markdown Mirror](#buildmarkdown_mirror--per-page-markdown-mirror-g15))
- `<script type="application/ld+json">` Schema.org blocks (only when `seo.json_ld_enabled = true` — see [JSON-LD Options](#json-ld-options))

All attribute values are XML-escaped. Inja renders missing variables as empty strings, so adding `{{ seo_meta }}` to existing templates is always safe.

If two pages alias the same URL, or an alias collides with a real page, **last-wins** (deterministic, since source files are sorted).

---

## Archetypes — `cstatic new`

The `cstatic new` command creates content files from archetype templates in the `archetypes/` directory. This is not a config key — it's a project workflow convention.

```bash
cstatic new posts/my-post.md              # uses archetypes/default.md
cstatic new --kind post posts/launch.md   # uses archetypes/post.md
```

### Resolution order

1. If `--kind` is given, load `archetypes/<kind>.md`.
2. Otherwise (or if the named archetype is missing), load `archetypes/default.md`.
3. If neither exists, a built-in default is used. An unknown `--kind` prints a warning and falls back to `default`.

### Placeholders

| Placeholder | Replaced with |
|-------------|---------------|
| `{{ title }}` | Title-cased filename stem (`my-cool-post` → `My Cool Post`) |
| `{{ slug }}` | Filename stem verbatim (`my-cool-post`) |
| `{{ date }}` | Today's date as `YYYY-MM-DD` |

Placeholder matching tolerates inner whitespace (`{{title}}`, `{{ title }}` both work). The output path is `<source_dir>/<path>` (so `source_dir = "src"` + `posts/x.md` → `src/posts/x.md`). Parent directories are created automatically. Existing files are never overwritten.

---

## `[check]` — Broken Link Verifier

`cstatic check` is a post-build verifier. After `cstatic build`, run it to confirm every internal link in the generated `output/` directory resolves to a real file (and optionally that every external URL is reachable). It exits with code `1` when issues are found, so it can gate CI.

```bash
cstatic check                 # internal links only
cstatic check --external      # also probe external URLs via HTTP HEAD
cstatic check --timeout 3000  # per-external-request timeout in ms
```

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `external` | bool | `false` | Verify external (`http://` / `https://` / `//host`) links via HTTP HEAD (GET fallback), following redirects. |
| `timeout_ms` | int | `5000` | Per-request timeout for external checks, in milliseconds. |

```toml
[check]
external = true
timeout_ms = 5000
```

The `--external` CLI flag overrides `external = false` in config; if either is set, external checks run. `--timeout` overrides `timeout_ms` when greater than `0`.

### URL classification

| URL shape | Handling |
|-----------|----------|
| `http://`, `https://`, `//host` | External — skipped unless `external` is on, then probed once per unique URL |
| Root-relative (`/posts/foo.html`) | Internal — resolved against `output/` and verified |
| `mailto:`, `tel:`, `sms:`, `data:`, `javascript:` | Skipped |
| `#fragment` only | Skipped (in-page anchor) |
| Relative (`other.html`) | Skipped in v1 |
| Trailing `/` or no extension (`/posts/`) | Resolved to `index.html` |

Fragment (`#…`) and query (`?…`) are stripped before resolving internal links. Transport-level failures (DNS, connection refused, timeout, missing HTTPS support in the binary) are reported as warnings on stderr and do **not** fail the check — only HTTP error statuses (`>= 400`) and missing internal files do.

