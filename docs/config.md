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

```toml
[site]
title = "My Site"
base_url = "https://example.com"
language = "en"
twitter_handle = "@username"
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

---

## `[modules]` — Built-in Modules

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `sitemap` | bool | `true` | Generate `sitemap.xml` with all published pages |
| `rss` | bool | `false` | Generate RSS feed at `feed.xml` |
| `robots` | bool | `false` | Generate `robots.txt` |

```toml
[modules]
sitemap = true
rss = false
robots = false
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

### Robots.txt Options

When `modules.robots = true`, these keys customize `robots.txt`:

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `modules.robots_user_agent` | string | `"*"` | User-agent directive value |
| `modules.robots_include_sitemap` | bool | `true` | Automatically add a `Sitemap:` line pointing to `sitemap.xml` |
| `modules.robots_disallow` | string[] | `[]` | List of `Disallow` paths |

```toml
[modules]
robots = true
modules.robots_user_agent = "*"
modules.robots_include_sitemap = true
modules.robots_disallow = ["/admin/", "/private/"]
```

---

## `[sitemap]` — Sitemap Options

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `exclude` | string[] | `[]` | URL paths to exclude from the sitemap |

```toml
[sitemap]
exclude = ["/404.html", "/private/"]
```

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

All attribute values are XML-escaped. Inja renders missing variables as empty strings, so adding `{{ seo_meta }}` to existing templates is always safe.

If two pages alias the same URL, or an alias collides with a real page, **last-wins** (deterministic, since source files are sorted).
