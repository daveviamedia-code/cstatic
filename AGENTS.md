# AGENTS.md — C-Static for AI Coding Agents

> This is the canonical, agent-optimized reference for building or maintaining a **C-Static** site. Read this file before writing C-Static config, frontmatter, or templates. It is self-contained: an agent reading only this file should accomplish most tasks without chasing links.

---

## 1. What C-Static is

C-Static is a fast, minimal static site generator written in C++17. It ships as a **single static binary** (`cstatic`) with subcommands `init`, `new`, `build`, `serve`, `check`. No Node.js or Python runtime is required.

## 2. Mental model

- **Markdown-first.** Pages are Markdown files with optional YAML frontmatter, under `src/`.
- **3-phase build.** Phase 1 parses every Markdown file and builds a `pages` array. Phase 2 renders each page through its Inja template (multi-threaded). Phase 3 writes output, then runs the asset pipeline and modules (sitemap/RSS/JSON Feed/robots/OG images).
- **Inja templates**, not Go templates / Liquid / Tera. See §9 gotchas.
- **Content-hash incremental cache** (`.cstatic_cache/hashes.json`). Only changed pages rebuild.
- **Config overlay.** `--env production` deep-merges `config.production.toml` onto `config.toml`.
- **Deploy.** `output/` is an assets-only Cloudflare Worker; `git push` to `main` triggers deploy.

## 3. Project layout (what goes where)

```
.
├── config.toml              # Site config (base). config.<env>.toml overlays via --env.
├── wrangler.jsonc           # Cloudflare Workers config (assets-only, runtime-templated by init).
├── src/                     # Markdown content (pages, posts). Tree ⇒ output URLs.
│   └── posts/               #   e.g. src/posts/hello.md → /posts/hello/
├── templates/               # Inja layouts + partials (templates/partials/).
├── shortcodes/              # {{< name >}} components, .html, rendered with Inja.
├── archetypes/              # cstatic new templates (default.md, <kind>.md).
├── static/                  # Pass-through assets (css/, js/, images/); minified + fingerprinted.
├── _data/                   # JSON/YAML data files for data-driven pages.
├── .github/workflows/       # deploy.yml — push-to-deploy to Cloudflare Workers.
└── output/                  # Generated site (gitignored). Do NOT edit by hand.
```

## 4. CLI reference

All commands run from the project root (the directory containing `config.toml`).

| Command | Flags | Effect |
|---------|-------|--------|
| `cstatic init` | `[--name "Title"]` | Scaffold a new site in the current dir. `--name` sets `config.toml` title **and** the Cloudflare Worker name (slugified: `"My Blog"`→`my-blog`, unique per account). |
| `cstatic new <path>` | `[--kind <name>]` | Create `src/<path>` from `archetypes/<kind>.md` (or `archetypes/default.md`). Refuses to overwrite existing files. |
| `cstatic build` | `--full` `--drafts` `-j,--jobs <N>` `-e,--env <name>` `-v,--verbose` `--watch` | Build site → `output/`. See flags below. |
| `cstatic serve` | `--port <N>` `--drafts` `-e,--env <name>` | Dev server at `http://localhost:3000` with live reload (default port 3000). |
| `cstatic check` | `--external` `--timeout <ms>` | Verify internal links in `output/` (and optionally external via HTTP HEAD). Exits `1` on issues — gates CI. |

**`build` flags:**
- `--full` — force clean rebuild, ignore the hash cache.
- `--drafts` — include `draft: true` pages (also bypasses scheduled-publishing filter for preview).
- `-j, --jobs <N>` — parallel render threads (`0` = auto).
- `-e, --env <name>` — build environment overlay (default `development`); sets `{{ site.env }}`.
- `-v, --verbose` — print per-phase build timing to stderr.
- `--watch` — rebuild on file changes, stay running until Ctrl+C (no HTTP server).

## 5. Common tasks (action recipes)

### 5.1 Add a blog post or page

```bash
cstatic new --kind post posts/my-post.md   # uses archetypes/post.md
```

`archetypes/post.md` supports `{{ title }}` (Title-cased stem), `{{ slug }}` (raw stem), `{{ date }}` (today, `YYYY-MM-DD`). Edit the result:

```markdown
---
title: My Post
layout: post            # optional; a [[collection]] template overrides if omitted
date: 2026-07-01        # future date ⇒ skipped until it arrives (unless publish_future)
draft: false
tags: [web, writing]
description: One-line summary for SEO/social cards.
---

Body in **Markdown**. Shortcodes and wikilinks expand here.
```

Then `cstatic build`. Omit `--kind` to use `archetypes/default.md`.

### 5.2 Add a content collection

In `config.toml`:

```toml
[[collection]]
name = "posts"              # matches src/posts/*.md
template = "post"           # default template for items
index_template = "posts-index"   # auto index at /posts/
url_pattern = "/blog/{{ slug }}/"   # optional; empty = auto from file path
sort_by = "date"
sort_order = "desc"
```

Create `templates/post.html` (item) and `templates/posts-index.html` (index). Any file under `src/posts/` with no explicit `layout` uses `template`. Items appear in `{{ collections.posts }}`. Index page receives `collection.name` + `collection.pages`. Add an `archetypes/post.md` for `cstatic new --kind post`.

### 5.3 Add a taxonomy

In `config.toml`:

```toml
[[taxonomy]]
key = "tags"               # frontmatter field to index
template = "tag"           # term page (/tags/webdev/)
index_template = "tags"    # index page (/tags/)
```

Create `templates/tag.html` (term page: `taxonomy.key`, `taxonomy.term`, `taxonomy.pages`) and `templates/tags.html` (index: `taxonomy.key`, `taxonomy.terms` = array of `{term, count, url}`). Use any frontmatter field as `key` (e.g. `category`); values may be a string or an array.

### 5.4 Add a shortcode

Shortcodes are `shortcodes/<name>.html` Inja templates. They expand to HTML **before** the markdown render.

- **Inline** (positional + named args): `{{< youtube dQw4w9WgXcQ >}}` or `{{< figure src="/x.png" alt="x" >}}`. Access via `{{ params.0 }}`, `{{ named.src }}`.
- **Block** (no params on the opener ⇒ block start; `{{ content }}` holds inner markdown): `{{< note >}}…{{< /note >}}`. Same-name blocks nest correctly (balanced-close search).

Render context: `params` (positional array), `named` (object), `content` (block inner HTML), and `page` (`title`, `url`, `slug`, `date`). Example `shortcodes/figure.html`:

```html
<figure>
  <img src="{{ named.src }}" alt="{{ named.alt }}">
  {% if named.caption %}<figcaption>{{ named.caption }}</figcaption>{% endif %}
</figure>
```

Shortcodes auto-disable when the directory is empty/missing; an unknown name prints a stderr notice and expands to nothing.

### 5.5 Add a template / partial / layout

- **Partial:** `{% include "nav" %}` loads `templates/partials/nav.html`; partials see the full context.
- **Layout / inheritance:** `{% extends "base" %}` + `{% block name %}…{% endblock %}`. **First-override-wins** (once a parent overrides a block verbatim, deeper children cannot re-override it — differs from Jinja2's last-override-wins; see §9).

```html
{# templates/post.html #}
{% extends "base" %}
{% block content %}
  <article>{{ page.content }}</article>
{% endblock %}
```

### 5.6 Configure SEO & feeds

```toml
[site]
base_url = "https://example.com"
twitter_handle = "@you"     # used by {{ seo_meta }} twitter:site

[modules]
sitemap = true              # /sitemap.xml (default true)
rss = true                  # /feed.xml
json_feed = true            # /feed.json
robots = true               # /robots.txt
llms_txt = true             # /llms.txt + /llms-full.txt (GEO; LLM crawlers)
modules.rss_title = "My Site Feed"          # shared with json_feed
modules.rss_description = "Latest posts"
modules.rss_item_count = 20
modules.json_feed_output = "feed.json"
modules.llms_txt_max_pages = 0              # 0 = no cap; llms-full.txt always complete
modules.llms_txt_exclude = ["/tags/*"]      # glob against page URLs

[seo]
json_ld_enabled = true                     # Schema.org JSON-LD (GEO keystone)
org_name = "My Site Inc"                   # enables site-wide Organization schema
org_logo = "/logo.png"
org_same_as = ["https://twitter.com/you"]
website_search_url_template = "/search?q={search_term_string}"

[og_images]
enabled = true             # per-page social cards from an SVG template
template = "og-default"    # templates/og-default.svg (scaffolded)

[sitemap]
exclude = ["/404.html"]    # URL paths to drop from sitemap
```

In every layout's `<head>`, add `{{ seo_meta }}` — it emits description, Open Graph, Twitter Card, and canonical tags; missing variables render empty, so it is always safe. When `seo.json_ld_enabled = true`, `{{ seo_meta }}` **also** appends Schema.org JSON-LD `<script>` blocks: a site-wide WebSite (+ Organization when `org_name` is set), a page-level schema whose `@type` auto-resolves from `page.type` / URL (`BlogPosting` for `/posts/...`, else `WebPage`; `Product`/`SoftwareApplication` mapped from commerce fields), a `BreadcrumbList` for nested URLs, and each `page.schema_extra` entry verbatim. An explicit `page.schema` object deep-merges over the auto-generated one. This is the keystone GEO feature — Google AI Overviews, ChatGPT, and Perplexity weight JSON-LD heavily when citing.

**Schema blocks** author `schema_extra` from markdown. `{% schema "FAQPage" %}...{% endschema %}` (with `##? question` headings), `{% schema "HowTo" %}` (with `##! step` headings), and `{% schema "Review" item="…" rating="N" %}` each emit visible HTML **and** a matching JSON-LD object that flows into `schema_extra`. No config flag; runs after shortcodes, before wikilinks/cmark. Unknown types `warn:` and pass content through. See `docs/config.md` → Schema Blocks.

### 5.7 Add data-driven pages

```toml
[data]
data_dir = "_data"

[[data_source]]
file = "products.json"          # under _data/
template = "product"
url_pattern = "/products/{{ slug }}/"
item_key = "slug"
per_page = 10                    # pagination (>0)
per_item = true                  # one page per item; item available as {{ item }}
```

`_data/products.json` is a JSON array of objects. `{{ pagination }}` exposes `page`, `total_pages`, `prev_url`, `next_url`, `items`. **Note:** data-driven pages are generated after Phase 1, so they are **not** indexed by wikilinks/backlinks.

### 5.8 Enable wikilinks & backlinks

```toml
[build.markdown]
wikilinks = true
```

`[[target]]` / `[[target|display]]` resolve (in order) by: exact filename stem → slugified target → lowercased title → exact `aliases` match. Unresolved → `<a class="wikilink-unresolved">display</a>` + stderr warning. Every page gains `page.backlinks` (array of `{url, title}`). Any title/alias/stem change invalidates all pages on the next incremental build (coarse-grained, by design).

### 5.9 Deploy

```bash
cstatic build --env production   # builds with config.production.toml overlay
git push origin main             # triggers .github/workflows/deploy.yml
```

One-time setup: add repo secrets `CLOUDFLARE_API_TOKEN` (Workers Scripts: Edit + Account: Read) and `CLOUDFLARE_ACCOUNT_ID`. The workflow downloads the `cstatic` release binary, runs `cstatic build --env production`, and deploys `output/` via `cloudflare/wrangler-action`. Manual alternative: `npx wrangler deploy`.

## 6. Config quick reference

Full reference: `docs/config.md`. Most-used keys:

| Section | Key | Default | Notes |
|---------|-----|---------|-------|
| `[site]` | `title`, `base_url`, `language`, `twitter_handle` | `language="en"` | `base_url` trailing slash stripped. |
| `[build]` | `source_dir`, `output_dir`, `template_dir`, `static_dir` | `src`/`output`/`templates`/`static` | |
| `[build]` | `fingerprint_assets` | `false` | Adds content hashes; `{{ asset() }}` resolves them. |
| `[build]` | `publish_future` | `false` | `false` ⇒ future-dated pages skipped. |
| `[build.incremental]` | `enabled`, `hash_file` | `true`, `.cstatic_cache/hashes.json` | |
| `[build.minify]` | `css`, `js`, `html` | `true`/`true`/`true` | **html defaults ON** — see §9. |
| `[build.highlight]` | `enabled`, `style` | `true`, `"github"` | `style` ∈ `github` \| `github-dark`. Writes `css/highlight.css`. |
| `[build.images]` | `optimize`, `max_width`, `quality`, `webp`, `avif` | `optimize=false` | webp/avif need `cwebp`/`avifenc` on PATH. |
| `[build.markdown]` | `extensions`, `shortcodes_dir`, `wikilinks` | all ext / `shortcodes` / `false` | `extensions` ∈ `table`,`tasklist`,`strikethrough`,`autolink`. |
| `[modules]` | `sitemap`, `rss`, `json_feed`, `robots`, `llms_txt` | `T`/`F`/`F`/`F`/`F` | Plus `rss_title`/`rss_description`/`rss_item_count`, `json_feed_output`, `llms_txt_description`/`llms_txt_max_pages`/`llms_txt_exclude`, `robots_*` (incl. `robots_ai_crawlers_mode` ∈ `off`\|`allow`\|`disallow`\|`custom` + `robots_ai_crawlers_custom`). `llms_txt` writes `/llms.txt` + `/llms-full.txt`; summary falls back to `site.description`. |
| `[og_images]` | `enabled`, `template`, `output_format`, `width`, `height`, `output_dir` | `F`/`og-default`/`png`/`1200`/`630`/`og` | PNG needs rsvg-convert/convert/inkscape. |
| `[seo]` | `json_ld_enabled` + `org_*` + `website_search_url_template` | `F` / empty | JSON-LD structured data. `org_name` enables Organization schema; `website_search_url_template` adds WebSite SearchAction. |
| `[sitemap]` | `exclude` | `[]` | URL paths to drop. |
| `[data]` | `data_dir` | `_data` | |
| `[[data_source]]` | `file`, `template`, `url_pattern`, `item_key`, `per_page`, `per_item` | — | Array of tables. |
| `[[collection]]` | `name`, `template`, `index_template`, `url_pattern`, `sort_by`, `sort_order` | `sort_by=date`,`desc` | Array of tables. |
| `[[taxonomy]]` | `key`, `template`, `index_template` | — | Array of tables. |
| `[hooks]` | `before_build`, `after_build` | `""` | Shell scripts; non-zero `before_build` aborts. |
| `[check]` | `external`, `timeout_ms` | `false`/`5000` | For `cstatic check`. |

**Build profile / overlay semantics** (`--env <name>` → `config.<name>.toml`): tables are **deep-merged**; scalars and arrays (including `[[collection]]`) are **replaced wholesale**. Missing overlay file ⇒ warning + base config unchanged. `{{ site.env }}` defaults to `"development"`.

## 7. Frontmatter reference

| Field | Default | Description |
|-------|---------|-------------|
| `title` | auto (filename) | Page title. |
| `layout` | `default` | Template to use (a `[[collection]] template` overrides if omitted). |
| `permalink` | auto | Custom URL path (e.g. `/custom/`). |
| `date` | — | `YYYY-MM-DD`; used for sorting; future dates skip the build unless `publish_future=true`. |
| `tags` | — | Array of strings (indexed by a `tags` taxonomy if defined). |
| `aliases` | — | Array of old URLs → redirect stubs (also resolve wikilinks). **Not** in `{{ pages }}`. |
| `draft` | `false` | Skipped during build. |
| `description` | excerpt | SEO description. |
| `image` | — | Open Graph image URL (relative `/` prefixed with `base_url`). |
| `canonical` | `base_url+url` | Canonical URL override. |
| `sitemap_changefreq` | — | e.g. `monthly`. |
| `sitemap_priority` | — | e.g. `0.8`. |
| `type` | — | Schema.org `@type` override (e.g. `"Product"`, `"Article"`). Read by JSON-LD when `seo.json_ld_enabled = true`. |
| `author` | — | String name; articles emit it as `{@type:Person, name}`. |
| `schema` | — | Object deep-merged over the auto-generated JSON-LD schema. Use `schema["@type"]` to override the type. |
| `schema_extra` | — | Array (or object) emitted verbatim as extra JSON-LD `<script>` blocks. Auto-populated by `{% schema %}` blocks (FAQPage/HowTo/Review). |
| `keywords` | — | Array or comma string; articles fall back to comma-joined `tags`. |

Any extra field becomes `page.<field_name>` in templates. Commerce fields (`brand`, `price`, `currency`, `availability`, `rating`, `reviewCount`) and app fields (`application_category`/`category`, `operating_system`) are read by the `Product`/`SoftwareApplication` JSON-LD builders.

## 8. Template context variables

| Variable | Description |
|----------|-------------|
| `site` | Site config: `title`, `base_url`, `language`, `env` (current env name), `twitter_handle`. |
| `page` | Current page: `title`, `url`, `content`, `date`, `tags`, `excerpt`, plus any extra frontmatter. Gains `backlinks` when wikilinks are on. |
| `pages` | All pages, sorted by date (newest first). Excludes drafts, future-scheduled, and alias redirect stubs. |
| `data` | All loaded data files, keyed by filename stem. |
| `item` | Current data item (data-driven pages with `per_item`). |
| `pagination` | `page`, `total_pages`, `prev_url`, `next_url`, `items` (paginated pages). |
| `collections` | All collections keyed by name, e.g. `{{ collections.posts }}`. |
| `collection` | On a collection index page: `name`, `pages`. |
| `taxonomy` | On taxonomy pages: `key`, `term`, `pages` (term) or `terms` (index). |
| `seo_meta` | Auto OG / Twitter Card / canonical meta tags. When `seo.json_ld_enabled = true`, also appends Schema.org JSON-LD `<script>` blocks (WebSite + Organization + page schema + BreadcrumbList + schema_extra). |

## 9. Agent gotchas (read before writing templates/config)

1. **Inja, not Go/Liquid/Tera.** `{{ var }}`, `{% for x in ys %}…{% endfor %}`, `{% if %}…{% endif %}`, `{% include "name" %}`. No Hugo-style piped filters (no `| upper`).
2. **Shortcodes & wikilinks expand BEFORE the markdown render.** Emitted HTML passes through (`CMARK_OPT_UNSAFE` is on). Don't wrap shortcode output in surrounding markdown that depends on it.
3. **`--env` overlays:** tables deep-merge; scalars and arrays (incl. `[[collection]]`) are replaced wholesale.
4. **Incremental cache can go stale.** When smoke-testing, run `cstatic build --full` (or clear `output/` **and** `.cstatic_cache/`) before concluding something is broken. `rm -rf output` alone may report pages cached-but-missing.
5. **HTML minifier defaults ON** (`build.minify.html = true`). It strips attribute quotes (`class="bl"`→`class=bl`). Tests/assertions on literal HTML should set it `false`.
6. **Future-dated pages are skipped** unless `build.publish_future = true` (or `--drafts` / the dev server). Reported as `(N scheduled)`.
7. **`{{ asset("path") }}`** returns the fingerprinted filename when `fingerprint_assets = true`, else the path unchanged.
8. **`{{ seo_meta }}`** is always safe to add — missing variables render empty.
9. **Aliased/redirect URLs don't appear in `{{ pages }}`** (they are redirect stubs, not content).
10. **Layouts/inheritance are first-override-wins.** `{% extends "base" %}` + `{% block name %}`; once a parent overrides a block verbatim, deeper children cannot re-override it (differs from Jinja2's last-override-wins).
11. **Data-driven pages are NOT indexed by wikilinks/backlinks** (generated after Phase 1). Resolution is exact-match only — no fuzzy matching.

## 10. Working on cstatic itself (C++ codebase)

Build and test the generator (separate from building a *site*):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCSTATIC_BUILD_TESTS=ON
cmake --build build -j
cd build && ctest --output-on-failure
```

Architecture and shipped-feature history live in `IMPLEMENTATION_PLAN.md`; the source tree is under `src/` (`main.cpp`, `cli/`, `config/`, `content/`, `data/`, `hash/`, `pipeline/`, `template/`, `assets/`, `modules/`, `server/`, `utils/`). The 3-phase build is in `src/pipeline/builder.cpp`. This section is for agents contributing to the C++ codebase; the rest of this file targets *using* cstatic to build sites.
