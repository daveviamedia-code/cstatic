# Configuration Reference

All settings live in `config.toml` at the project root.

---

## `[site]` — Site Metadata

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `title` | string | — | Yes | Site title, available in templates as `{{ site.title }}` |
| `base_url` | string | — | Yes | Full URL (e.g. `"https://example.com"`). Trailing slash is stripped automatically. |
| `language` | string | `"en"` | No | Language code, used in `<html lang="{{ site.language }}">` |

```toml
[site]
title = "My Site"
base_url = "https://example.com"
language = "en"
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

```toml
[build.minify]
css = true
js = true
```

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
