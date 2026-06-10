# C-Static

A fast, minimal static site generator written in C++17.

[![Build & Test](https://github.com/daveviamedia-code/cstatic/actions/workflows/build.yml/badge.svg)](https://github.com/daveviamedia-code/cstatic/actions/workflows/build.yml)

## Features

- **Markdown-first** — Write pages in Markdown with YAML frontmatter
- **Template engine** — Inja-based templates with full context (site, page, data, pagination)
- **Data-driven pages** — Generate pages from JSON/YAML data sources with pagination and per-item routing
- **Incremental builds** — Content-hash caching means only changed pages are rebuilt
- **Asset pipeline** — Built-in CSS/JS minification with incremental support
- **Dev server** — Live-reload development server with file watching
- **Built-in modules** — Sitemap.xml, RSS feed, and robots.txt generation
- **Custom 404** — Automatic 404 page, or override with `src/404.md`
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
cstatic init
```

This creates:

```
.
├── config.toml           # Site configuration
├── src/
│   ├── index.md          # Home page
│   └── about.md          # Sample page
├── templates/
│   └── default.html      # Layout template
└── static/
    ├── css/style.css     # Minimal reset
    └── js/app.js         # Placeholder
```

### Build & Preview

```bash
cstatic build              # Build the site → output/
cstatic serve              # Dev server at http://localhost:3000
cstatic serve --port 8080  # Custom port
cstatic build --full       # Force full rebuild (ignores cache)
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
```

### Frontmatter Fields

| Field        | Default     | Description                              |
|-------------|-------------|------------------------------------------|
| `title`     | Auto       | Page title (falls back to filename)      |
| `layout`    | `default`  | Template to use                          |
| `permalink` | Auto       | Custom URL path (e.g. `/custom/`)        |
| `date`      | —          | ISO date string for sorting              |
| `tags`      | —          | Array of tag strings                     |
| `draft`     | `false`    | If true, page is skipped during build    |

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

## Built-in Modules

| Module    | Config key         | Output                |
|----------|--------------------|-----------------------|
| Sitemap  | `modules.sitemap`  | `/sitemap.xml`        |
| RSS      | `modules.rss`      | `/rss.xml`            |
| Robots   | `modules.robots`   | `/robots.txt`         |
| 404      | Automatic          | `/404.html`           |

## Building from Source

### Prerequisites

- CMake 3.16+
- C++17 compiler (Clang, GCC, or MSVC)

All dependencies are fetched automatically via CMake FetchContent:

CLI11, toml++, yaml-cpp, cmark, Inja, xxHash, cpp-httplib

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
├── main.cpp              # CLI entry point (init, build, serve)
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
