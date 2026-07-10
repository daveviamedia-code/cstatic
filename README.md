# C-Static

A fast, minimal static site generator written in C++17.

[![Build & Test](https://github.com/daveviamedia-code/cstatic/actions/workflows/build.yml/badge.svg)](https://github.com/daveviamedia-code/cstatic/actions/workflows/build.yml)

## Features

- **Markdown-first** — Write pages in Markdown with YAML frontmatter
- **GFM extensions** — Tables, task lists, strikethrough, and autolinks via cmark-gfm
- **Shortcodes** — Reusable content components via `{{< name params >}}` syntax, expanded before the markdown render pass
- **Syntax highlighting** — Built-in code highlighting for 10+ languages with light/dark themes
- **Template engine** — Inja-based templates with full context (site, page, data, pagination)
- **Data-driven pages** — Generate pages from JSON/YAML data sources with pagination and per-item routing
- **Content collections** — Define content types with default templates, URL patterns, sorting, and auto-generated index pages
- **Taxonomy pages** — Automatic tag and category index pages (`/tags/webdev/`, `/categories/tutorials/`)
- **Build profiles** — Per-environment config overlays via `--env` (e.g. `config.production.toml`) with `{{ site.env }}` template variable
- **Page aliases** — Frontmatter `aliases` array generates HTML redirect pages at old URLs, included in sitemap
- **SEO meta tags** — Automatic Open Graph, Twitter Card, and canonical link tags via `{{ seo_meta }}` template variable
- **Schema.org JSON-LD** — Opt-in structured data (`seo.json_ld_enabled`) emits WebSite + Organization + per-page schemas (auto-typed: `BlogPosting`, `Article`, `Product`, `SoftwareApplication`, …) + `BreadcrumbList` for GEO/AI-search citation
- **Schema blocks** — `{% schema "FAQPage" %}` / `"HowTo"` / `"Review"` blocks in markdown render visible HTML **and** emit the matching JSON-LD into `page.schema_extra` (Q&A and how-to content that AI search engines love to cite)
- **Standalone FAQ extraction** — `##? question` headings anywhere in a page auto-build a `FAQPage` (visible `<details>` HTML + JSON-LD + `{{ page.faq }}`), no wrapper required; merges with any `{% schema "FAQPage" %}` block into one FAQPage
- **E-E-A-T author entities** — `authors.enabled = true` loads `src/authors/*.md` into an index; page frontmatter `author: <slug>` resolves to a full author object (`{{ page.author }}`) and a Schema.org `Person` JSON-LD object, with a generated profile page per author at `/authors/<slug>/`
- **Citation meta tags** — `seo.citation_tags_enabled = true` emits `citation_*` meta tags (Google Scholar, Perplexity, ChatGPT): author, title, publication/online dates, PDF URL, abstract, journal, DOI, and semicolon-joined keywords — all auto-derived from frontmatter
- **Passage index** — Every page exposes `{{ page.passages }}` (`[{id, heading, text, level}]`) auto-extracted from `<h2>`–`<h6>` headings; when `seo.json_ld_enabled = true`, each passage is also emitted as a `WebPageElement` under the page schema's `hasPart` so AI engines can cite specific passages by anchor
- **Auto Table of Contents** — Injects `id` attributes into heading tags and builds `{{ page.toc }}` (nested tree); insert `<!--toc-->` anywhere in content to render a `<nav>` TOC at that position — IDs match passage anchors exactly (shared slugify)
- **TL;DR / Key Takeaways** — `tldr` frontmatter (string) overrides the meta description and schema description (priority: tldr → description → excerpt); `key_takeaways` frontmatter (string array) populates a JSON-LD `mainEntity` `ItemList` of key points — both available to templates as `{{ page.tldr }}` / `{{ page.key_takeaways }}`
- **Brand mention normalization** — When `seo.org_name` is set, C-Static validates the Organization identity (logo file existence, same_as URL format, founders vs known author slugs, name vs site_title divergence) and exposes `{{ site.org }}` so footers and contact blocks render from a single source of truth
- **OG image generation** — Per-page social-card images from Inja SVG templates, converted to PNG via rsvg-convert/ImageMagick/Inkscape
- **Content scaffolding** — `cstatic new` creates pages from archetypes (`archetypes/<kind>.md`) with `{{ title }}`, `{{ slug }}`, and `{{ date }}` placeholders
- **Scheduled publishing** — Pages with a future `date` are automatically skipped until their date arrives (toggle with `build.publish_future`)
- **Broken link checker** — `cstatic check` scans built output and verifies internal links against the filesystem (and optionally external URLs via HTTP HEAD), exiting non-zero so it can gate CI
- **Better error rendering** — Frontmatter and template errors show ±3 lines of source context with a `>` marker and a `^` caret pointing at the column; `cstatic build -v` prints per-phase timing
- **Wikilinks & backlinks** — `[[page]]` syntax resolves by filename stem, lowercase title, or frontmatter alias; `page.backlinks` exposes the reverse relationship to templates
- **Search index** — Optional client-side search index (`search-index.json`) for Lunr.js/Fuse.js integration
- **Incremental builds** — Content-hash caching means only changed pages are rebuilt
- **Asset pipeline** — Built-in CSS/JS minification with incremental support
- **Image optimization** — Resize, recompress, and convert images (WebP/AVIF) via stb
- **Asset fingerprinting** — Content-based cache-busting with `{{ asset() }}` template helper
- **Dev server** — Live-reload development server with file watching
- **Watch mode** — `cstatic build --watch` rebuilds on file changes without running an HTTP server (useful for CI previews or proxying through a separate static server)
- **Built-in modules** — Sitemap.xml, RSS feed, JSON Feed, and robots.txt generation
- **Custom 404** — Automatic 404 page, or override with `src/404.md`
- **Cloudflare Workers deploy** — `cstatic init` scaffolds `wrangler.jsonc` + a GitHub Actions workflow for push-to-deploy (Local → GitHub → Cloudflare Workers)
- **Zero runtime deps** — Single static binary, no Node.js or Python required

## Quick Start

### Install from Source

```bash
git clone https://github.com/daveviamedia-code/cstatic.git
cd cstatic
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

### Scaffold a New Site

```bash
mkdir my-site && cd my-site
cstatic init                      # site title defaults to "My Site"
cstatic init --name "My Blog"     # custom title → config.toml + Cloudflare Worker name
```

`--name` sets the site title in `config.toml` *and* the Cloudflare Worker name in `wrangler.jsonc` (slugified: `"My Blog"` → `my-blog`). Worker names are unique per Cloudflare account, so give each site a distinct name to avoid deploy collisions.

This creates:

```
.
├── config.toml           # Site configuration
├── wrangler.jsonc        # Cloudflare Workers config (assets-only)
├── .gitignore            # Ignores output/, cache, node_modules/
├── src/
│   ├── index.md          # Home page
│   ├── about.md          # Sample page
│   └── posts/
│       └── first-post.md # Sample blog post
├── templates/
│   ├── default.html      # Layout template
│   ├── post.html         # Blog post template
│   ├── posts-index.html  # Blog index template
│   ├── tag.html          # Tag listing template
│   ├── tags.html         # Tag index template
│   └── og-default.svg    # Open Graph image template (1200×630)
├── shortcodes/
│   ├── youtube.html      # {{< youtube ID >}}
│   ├── figure.html       # {{< figure src="..." alt="..." >}}
│   └── note.html         # {{< note >}}...{{< /note >}}
├── archetypes/
│   ├── default.md        # `cstatic new` template (title/date placeholders)
│   └── post.md           # `cstatic new --kind post` template (starts as draft)
├── static/
    ├── css/style.css     # Minimal reset
    └── js/app.js         # Placeholder
└── .github/
    └── workflows/
        └── deploy.yml    # Push-to-deploy to Cloudflare Workers
```

### Create New Pages

Generate content from archetypes — great for consistent frontmatter across posts:

```bash
cstatic new posts/my-post.md              # uses archetypes/default.md
cstatic new --kind post posts/launch.md   # uses archetypes/post.md
cstatic new about.md                      # top-level page
```

The filename stem derives the title (`my-post` → `My Post`) and slug. Archetypes support three placeholders: `{{ title }}`, `{{ slug }}`, and `{{ date }}` (today's date, `YYYY-MM-DD`). C-Static refuses to overwrite existing files.

### Build & Preview

```bash
cstatic build              # Build the site → output/
cstatic serve              # Dev server at http://localhost:3000
cstatic serve --port 8080  # Custom port
cstatic build --full       # Force full rebuild (ignores cache)
cstatic build -v           # Verbose: print per-phase build timing
cstatic build --watch      # Rebuild on file changes (stay running until Ctrl+C)
cstatic build --env production  # Build with config.production.toml overlay
cstatic check              # Verify internal links in output/ (exits 1 on broken links)
cstatic check --external   # Also probe external URLs via HTTP HEAD
```

## Configuration

Site settings live in `config.toml`:

```toml
[site]
title = "My Site"
base_url = "https://example.com"
language = "en"

[build]
source_dir = "src"
output_dir = "output"
template_dir = "templates"
static_dir = "static"

[build.incremental]
enabled = true
hash_file = ".cstatic_cache/hashes.json"

[build.minify]
css = true
js = true
html = true

[build.highlight]
enabled = true
style = "github"       # or "github-dark"

[build.images]
optimize = true
max_width = 1920
quality = 85
webp = true

[build]
fingerprint_assets = true

[modules]
sitemap = true
rss = false
robots = false
```

See the [full config reference](docs/config.md) for all options.

## Writing Pages

Pages are Markdown files with optional YAML frontmatter:

```markdown
---
title: Hello World
layout: default
date: "2025-01-15"
tags:
  - hello
  - world
custom_field: any value
---

# Hello World

Your markdown content here.

## GFM Extensions

Tables, task lists, strikethrough, and autolinks are supported out of the box:

```markdown
| Feature | Status |
|--------|--------|
| Tables | ✓ |

- [x] Done
- [ ] Todo

~~struck through~~ and https://example.com (auto-linked)
```

## Syntax Highlighting

Fenced code blocks with a language hint are highlighted automatically. Include the stylesheet in your template:

```html
<link rel="stylesheet" href="/css/highlight.css">
```

````markdown
```js
function greet(name) {
  console.log("Hello, " + name);
}
```
````

### Frontmatter Fields

| Field        | Default     | Description                              |
|-------------|-------------|------------------------------------------|
| `title`     | Auto       | Page title (falls back to filename)      |
| `layout`    | `default`  | Template to use                          |
| `permalink` | Auto       | Custom URL path (e.g. `/custom/`)        |
| `date`      | —          | ISO date string (`YYYY-MM-DD`) for sorting; future dates skip the build unless `publish_future = true` |
| `tags`      | —          | Array of tag strings                     |
| `aliases`   | —          | Array of old URLs to redirect to this page |
| `draft`     | `false`    | If true, page is skipped during build    |
| `description` | —        | SEO description (falls back to excerpt) |
| `image`     | —          | Open Graph image URL (relative or absolute) |
| `canonical` | —          | Canonical URL override |
| `sitemap_changefreq` | — | Sitemap change frequency (e.g. `monthly`) |
| `sitemap_priority`   | — | Sitemap priority (e.g. `0.8`) |

Any extra fields are available in templates as `page.field_name`.

## Templates

Templates use [Inja](https://github.com/pantor/inja) syntax:

```html
<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
  <title>{{ page.title }} — {{ site.title }}</title>
</head>
<body>
  <nav>
    {% for p in pages %}
    <a href="{{ p.url }}">{{ p.title }}</a>
    {% endfor %}
  </nav>
  <main>
    {{ page.content }}
  </main>
</body>
</html>
```

### Template Context

| Variable   | Description                                      |
|-----------|--------------------------------------------------|
| `site`    | Site config (title, base_url, language)          |
| `page`    | Current page metadata + content                  |
| `pages`   | All pages (sorted by date, newest first)         |
| `data`    | All loaded data files (keyed by filename stem)   |
| `item`    | Current data item (data-driven pages only)       |
| `pagination` | Pagination object (paginated pages only)      |
| `collections` | All collections keyed by name (e.g. `{{ collections.posts }}`) |
| `seo_meta` | Auto-generated Open Graph, Twitter Card, and canonical meta tags |

### Asset Fingerprinting

Use `{{ asset("path") }}` to reference assets with automatic cache-busting:

```html
<link rel="stylesheet" href="{{ asset("css/style.css") }}">
```

When `fingerprint_assets = true`, this resolves to a hashed filename like `css/style.a3f7b2c1.css`. Otherwise, it returns the path unchanged.

### Template Partials

Reuse HTML fragments across templates with `{% include "name" %}`:

```html
{# in templates/default.html #}
{% include "nav" %}
<main>{{ page.content }}</main>
```

Place partials in `templates/partials/`:

```
templates/
├── default.html
└── partials/
    └── nav.html
```

The partial name is the filename stem (e.g. `nav.html` → `{% include "nav" %}`). Partials have access to the full template context.

## Data-Driven Pages

Generate pages from JSON or YAML data:

```toml
[data]
data_dir = "_data"

[[data_source]]
file = "products.json"
template = "product"
url_pattern = "/products/{{ slug }}/"
item_key = "slug"
per_page = 10
per_item = true
```

Place data files in `_data/`:

```json
[
  {"slug": "widget", "name": "Widget", "price": 9.99},
  {"slug": "gadget", "name": "Gadget", "price": 19.99}
]
```

## Content Collections

Define content types with their own templates, URL patterns, and auto-generated index pages:

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
```

Any markdown file under `src/posts/` automatically uses the `post` template and appears in `{{ collections.posts }}`. An index page is generated at `/posts/index.html` using the `posts-index` template.

## Taxonomy Pages

Generate automatic index pages for tags, categories, or any frontmatter field:

```toml
[[taxonomy]]
key = "tags"
template = "tag"
index_template = "tags"
```

This produces:
- `/tags/` — index page listing all tags with counts
- `/tags/webdev/` — term page listing all pages tagged `webdev`

### Taxonomy Template Context

**Term page** (`tag.html`) receives `taxonomy.key`, `taxonomy.term`, and `taxonomy.pages`.

**Index page** (`tags.html`) receives `taxonomy.key` and `taxonomy.terms` (array of `{term, count, url}`).

### Custom Taxonomies

You can define taxonomies for any frontmatter field:

```toml
[[taxonomy]]
key = "category"
template = "category"
index_template = "categories"
```

Then use `category: tutorials` in frontmatter (string) or `category: [web, dev]` (array).

## Built-in Modules

| Module    | Config key            | Output                |
|----------|-----------------------|-----------------------|
| Sitemap  | `modules.sitemap`     | `/sitemap.xml`        |
| RSS      | `modules.rss`         | `/feed.xml`           |
| JSON Feed| `modules.json_feed`   | `/feed.json`          |
| Robots   | `modules.robots`      | `/robots.txt`         |
| 404      | Automatic             | `/404.html`           |

## Deployment

`cstatic init` scaffolds everything you need to deploy to **Cloudflare Workers** via `git push` — no manual config required. The route is **Local → GitHub → Cloudflare Workers**.

- **`wrangler.jsonc`** — an assets-only Worker. C-Static's `output/` is served directly (no Worker script runs), with `/404.html` handling unmatched paths.
- **`.github/workflows/deploy.yml`** — on push to `main`, downloads the latest `cstatic` release binary, runs `cstatic build --env production`, and deploys `output/` to Cloudflare Workers.

### One-time setup

1. Add two repository secrets (*Settings → Secrets and variables → Actions*):
   - `CLOUDFLARE_API_TOKEN` — a token with **Workers Scripts: Edit** + **Account: Read**
   - `CLOUDFLARE_ACCOUNT_ID` — your Cloudflare account ID
2. Push to `main`. The first run creates the Worker; later runs update it.
3. *(Optional)* Add a custom domain in the Cloudflare dashboard, or a `config.production.toml` overlay for production-only settings.

> The workflow fetches `cstatic` from `CSTATIC_REPO` (default `daveviamedia-code/cstatic`, set at the top of `deploy.yml`). Change it if you fork or publish the binary elsewhere.

To deploy manually instead:

```bash
cstatic build --env production
npx wrangler deploy
```

## Building from Source

### Prerequisites

- CMake 3.16+
- C++17 compiler (Clang, GCC, or MSVC)

All dependencies are fetched automatically via CMake FetchContent:

CLI11, toml++, yaml-cpp, cmark, Inja, xxHash, cpp-httplib, stb

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Run Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCSTATIC_BUILD_TESTS=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

### Install

```bash
sudo cmake --install build
```

This installs the `cstatic` binary to `/usr/local/bin/`.

## Architecture

```
src/
├── main.cpp              # CLI entry point (init, new, build, serve)
├── cli/                  # cstatic new content generator
├── config/               # TOML config parsing & validation
├── content/              # Markdown rendering & frontmatter parsing
├── data/                 # JSON/YAML data loading
├── hash/                 # XXH64 hash store for incremental builds
├── pipeline/             # Core build pipeline (3-phase)
├── template/             # Inja template rendering
├── assets/               # Static asset pipeline with minification
├── modules/              # Sitemap, RSS, robots.txt generators
├── server/               # Development HTTP server
└── utils/                # Path helpers, terminal colors
```

## License

MIT
