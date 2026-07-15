---
name: cstatic-configure-seo
description: Configure SEO and feeds for a C-Static site. Use when the user says "enable sitemap", "RSS feed", "JSON feed", "robots.txt", "OG images", "social cards", "seo_meta", "SEO setup", "AI crawlers", "GEO", "JSON-LD", "structured data", "Schema.org", or "allow GPTBot/ClaudeBot". Covers modules, {{ seo_meta }}, og_images, AI crawler allowlist, JSON-LD structured data, and sitemap excludes.
---

# Skill: Configure SEO & feeds

C-Static can generate a sitemap, RSS feed, JSON Feed, robots.txt, and per-page social-card images, and emit standard meta tags via `{{ seo_meta }}`.

## Steps

1. **Enable modules** in `config.toml`:

   ```toml
   [modules]
   sitemap = true                 # /sitemap.xml  (default true)
   rss = true                     # /feed.xml
   json_feed = true               # /feed.json
   robots = true                  # /robots.txt

   # Shared feed metadata (rss + json_feed both use these):
   rss_title = "My Site Feed"
   rss_description = "Latest posts from My Site"
   rss_item_count = 20
   json_feed_output = "feed.json"

   # robots.txt tuning:
   robots_user_agent = "*"
   robots_include_sitemap = true
   robots_disallow = ["/admin/"]

   # AI/LLM crawlers (GEO): "off" | "allow" | "disallow" | "custom".
   # "allow" emits Allow:/ for GPTBot, ClaudeBot, PerplexityBot, etc. so AI
   # search (ChatGPT, Perplexity, Google AI Overviews) can cite your pages.
   robots_ai_crawlers_mode = "allow"
   # "custom" allows only the listed agents:
   # robots_ai_crawlers_mode = "custom"
   # robots_ai_crawlers_custom = ["GPTBot", "ClaudeBot"]
   ```

2. **Add `{{ seo_meta }}` to every layout's `<head>`** (typically in `templates/base.html` or `templates/default.html`). It emits `<meta name="description">`, Open Graph (`og:title`, `og:description`, `og:url`, `og:image`), Twitter Card, and `<link rel="canonical">`. Missing variables render empty, so adding it is always safe.

   ```html
   <head>
     <title>{{ page.title }} — {{ site.title }}</title>
     {{ seo_meta }}
   </head>
   ```

3. **(Optional) Generate per-page OG images** from an SVG template:

   ```toml
   [og_images]
   enabled = true
   template = "og-default"     # templates/og-default.svg (scaffolded, 1200x630)
   output_format = "png"       # "png" (needs converter) | "svg"
   width = 1200
   height = 630
   output_dir = "og"
   ```

   - The generated image is injected into `{{ seo_meta }}` `og:image` (when the page has no explicit `image` frontmatter), into `sitemap.xml`, and into the RSS feed.
   - PNG output needs one of: `rsvg-convert`, `convert` (ImageMagick), `inkscape` on PATH. If none, SVG is written instead.
   - The SVG template receives `page.title`, `page.date`, `page.excerpt`, `page.url`, `site.title`, `site.base_url`.

4. **(Optional) Exclude paths from the sitemap:**

   ```toml
   [sitemap]
   exclude = ["/404.html", "/private/"]
   ```

5. **Set site-level SEO fields** (`[site]`):

   ```toml
   [site]
   base_url = "https://example.com"     # REQUIRED for absolute URLs in feeds/og
   twitter_handle = "@you"              # → twitter:site in {{ seo_meta }}
   ```

6. **Per-page SEO overrides** via frontmatter: `description`, `image`, `canonical`, `sitemap_changefreq`, `sitemap_priority`.

7. **(Optional, GEO keystone) Enable Schema.org JSON-LD** so AI search engines (Google AI Overviews, ChatGPT, Perplexity, Bing Copilot) can richly cite your pages. When on, `{{ seo_meta }}` appends `<script type="application/ld+json">` blocks: site-wide WebSite (+ Organization when `seo.org_name` is set), a page schema whose `@type` auto-resolves from `page.type` / URL, a `BreadcrumbList` for nested pages, and each `page.schema_extra` entry verbatim. Default off preserves existing output.

   ```toml
   [seo]
   json_ld_enabled = true
   org_name = "My Site Inc"            # enables site-wide Organization schema
   org_logo = "/logo.png"
   org_founding_date = "2015-01-01"
   org_founders = ["Alice"]
   org_same_as = ["https://twitter.com/you"]
   website_search_url_template = "/search?q={search_term_string}"
   ```

   Per-page frontmatter drives the page schema:

   ```markdown
   ---
   title: Launch Day
   type: Article           # @type override (BlogPosting auto-inferred for /posts/...)
   author: Jane Doe        # → {@type:Person, name}
   keywords: [launch, news]
   schema:                 # deep-merged over the auto-generated schema
     isAccessibleForFree: true
   schema_extra:           # array emitted verbatim as extra <script> blocks
     - "@type": FAQPage
       mainEntity:
         "@type": Question
         name: What is this?
   ---
   ```

   For commerce pages use `type = "Product"` (or `"SoftwareApplication"`) plus `brand`, `price`, `currency`, `availability`, `rating`, `reviewCount`, and (for apps) `application_category` / `operating_system`. Missing required fields surface as non-fatal `warn:` lines on stderr.

8. **(Optional, E-E-A-T) Enable author entities** so each byline resolves to a full person with a profile page. Create `src/authors/<slug>.md` files (stem = slug) with `name`, `title`, `bio`, `avatar`, `email`, `twitter`/`linkedin`/`github`, `website`, `same_as[]`, `expertise[]`. Then on any page set `author: <slug>` in frontmatter — it resolves to `{{ page.author }}` (full object) and a `Person` JSON-LD schema (when JSON-LD is also on). Each author gets a profile page at `/authors/<slug>/` rendered with `templates/author.html` (`{{ author.posts }}` lists their published pages).

   ```toml
   [authors]
   enabled = true
   ```

9. **(Optional, citation tags) Enable `citation_*` meta tags** for Google Scholar / Perplexity / ChatGPT citation. Auto-derives `citation_author`, `citation_title`, `citation_publication_date`, `citation_online_date` (from `created`, falls back to `date`), `citation_pdf_url`, `citation_abstract` (from `tldr`, falls back to `description`), `citation_journal_title`, `citation_doi`, and `citation_keywords` (tags semicolon-joined). Missing fields are omitted.

   ```toml
   [seo]
   citation_tags_enabled = true
   ```

   Per-page frontmatter fields (all optional, read from custom frontmatter):

   ```yaml
   ---
   title: A Study of X
   date: 2025-06-01
   created: 2025-05-15
   author: jane-doe
   pdf_url: https://example.com/paper.pdf
   journal: Journal of ML
   doi: 10.1000/xyz123
   tldr: A one-sentence summary used as the citation abstract.
   tags: [machine-learning, neural-networks]
   ---
   ```

10. **Passage index (automatic — nothing to enable).** Every page's `<h2>`–`<h6>` headings are auto-extracted into `{{ page.passages }}` — an array of `{id, heading, text, level}`:

    ```html
    <ul class="toc">
    {% for p in page.passages %}
      <li><a href="#{{ p.id }}">{{ p.heading }}</a> <small>(L{{ p.level }})</small></li>
    {% endfor %}
    </ul>
    ```

    - `id` = slugified heading text (`"Hello, World!"` → `"hello-world"`); collisions get `-1`, `-2`, … suffixes.
    - `text` = body-until-next-heading, HTML stripped, whitespace collapsed, ≤500 chars.
    - `level` = 2–6 (`<h1>` is skipped as the page title).
    - When `seo.json_ld_enabled = true` (step 1), each passage is also emitted as a `WebPageElement` under the page schema's `hasPart`, with `url = <canonical or base_url + page.url>#<id>` — AI engines get machine-readable passage boundaries + anchor targets.
    - An explicit `page.schema.hasPart` in frontmatter overrides the auto-generated array (deep-merge).

11. **Auto TOC (automatic — nothing to enable).** C-Static injects `id="..."` attributes into `<h2>`–`<h6>` tags (cmark-gfm doesn't emit them) and builds `{{ page.toc }}` — a nested tree of `{id, text, level, children: [...]}`. IDs use the same slugify as passage index, so `#anchor` links and passage `hasPart` URLs resolve to the same heading.

    - Insert `<!--toc-->` (or `<!-- toc -->`) anywhere in your markdown to render a `<nav class="toc"><ul>…</ul></nav>` at that position.
    - Or use `{{ page.toc }}` in templates for custom TOC rendering:

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

    - Duplicate headings get `-1`, `-2`, … suffixes (matching G8 passage IDs). Headings with existing `id` attributes are preserved.
    - Skip-level nesting (h2 → h4) nests the h4 directly under the h2.

12. **TL;DR / Key Takeaways (automatic — nothing to enable).** Two frontmatter fields improve AI citability:
    - `tldr` (string) — overrides `description`/`excerpt` as both the `<meta name="description">` and the JSON-LD schema `description`. Priority: tldr → description → excerpt.
    - `key_takeaways` (array of strings) — when non-empty and `seo.json_ld_enabled = true`, the page schema gains a `mainEntity` `ItemList` of `ListItem` entries (each with `position` + `name`). An explicit `page.schema.mainEntity` overrides it.
    - Both available as `{{ page.tldr }}` / `{{ page.key_takeaways }}`.
    - Scaffold shortcodes: `{{< tldr >}}…{{< /tldr >}}` and `{{< takeaways >}}…{{< /takeaways >}}` wrap the content in styled HTML for visible rendering.

    ```yaml
    ---
    tldr: "A one-sentence summary."
    key_takeaways:
      - Point one
      - Point two
    ---
    ```

13. **Brand mention normalization (automatic when `org_name` is set).** When you configure `seo.org_name`, C-Static validates the Organization identity once per build and exposes `{{ site.org }}` so footers, contact blocks, and about pages all render from a single source of truth — no duplicated name/logo/URL strings across templates.

    Validation checks (non-fatal `warn:` on stderr; doesn't fail the build):
    - `org_name` diverging from `site_title` (informational — intentional when the org and site have different names).
    - `org_logo` local-path file not found under `static_dir` (absolute URLs starting with `https://` or `http://` are skipped).
    - `org_same_as` entries that don't look like URLs (no `://`).
    - `org_founders` entries that don't match a known author slug (checked only when `authors.enabled = true` and the index is non-empty).

    `{{ site.org }}` template object (only non-empty fields included):

    ```html
    <footer>
      {% if site.org.name %}
      <p>{{ site.org.name }} &mdash; founded {{ site.org.founding_date }}</p>
      <img src="{{ site.org.logo_url }}" alt="{{ site.org.name }} logo">
      {% for link in site.org.same_as %}
      <a href="{{ link }}">{{ link }}</a>
      {% endfor %}
      {% endif %}
    </footer>
    ```

14. **Reading time / word count / difficulty (automatic — nothing to enable).** Every page gets three cheap computed fields derived from its rendered HTML, exposed as template variables:

    ```html
    <p class="meta">
      {{ page.word_count }} words · {{ page.reading_time }} min read · {{ page.difficulty }}
    </p>
    ```

    - `{{ page.word_count }}` (int) — whitespace-separated words + CJK ideographs (each CJK char counts as one word, since CJK text isn't whitespace-separated).
    - `{{ page.reading_time }}` (int) — estimated minutes at 200 words/minute (`ceil(word_count / 200)`).
    - `{{ page.difficulty }}` (string) — `"easy"`, `"moderate"`, `"difficult"`, `"very-difficult"` via the Flesch reading-ease heuristic, or empty string when not computable.
    - `<pre>` and `<code>` blocks are stripped before counting — code isn't prose and would skew the syllable counter.
    - Flesch difficulty is English-specific: CJK-dominated text (≥ half of counted words are CJK chars) yields an empty `difficulty` string.
    - When `seo.json_ld_enabled = true` (step 1), Article-typed pages (BlogPosting/Article/NewsArticle/TechArticle) also emit `wordCount` on the JSON-LD schema, and any page with a reading time emits `timeRequired` as an ISO 8601 duration (`PT5M` = 5 minutes). Explicit `page.schema.wordCount` or `page.schema.timeRequired` in frontmatter overrides via deep-merge.

15. **AI sitemap (`modules.sitemap_ai`, default off).** When enabled, C-Static generates a curated `/sitemap-ai.xml` alongside the standard `/sitemap.xml` — a second sitemap for AI crawlers (ChatGPT, Perplexity, Google AI Overviews) that filters out thin pages:

    ```toml
    [modules]
    sitemap_ai = true

    [sitemap_ai]
    include_images = true       # default; embed <image:image> from og_image + image
    exclude_types = ["landing"] # default []; drop pages with these page.type values
    ```

    A page must pass ALL filters to appear in `sitemap-ai.xml`:
    - Not matching `sitemap.exclude` globs (inherited from the standard sitemap).
    - URL doesn't contain `/tags/`, `/categories/`, or `/page/` (taxonomy listings + paginated indexes).
    - `word_count > 100` (from step 14 — naturally excludes taxonomy pages which lack word_count).
    - `page.type` not in `sitemap_ai.exclude_types`.

    When `include_images = true`, each `<url>` block gains deduped `<image:image>` entries from `og_image` + `image` (relative URLs resolved to absolute via `site.base_url`). The `xmlns:image` namespace is only declared when at least one included page has images.

16. **`.well-known/` discovery files (`well_known.*`, all default off).** Two independently opt-in generators write standard discovery files to `/.well-known/`:

    ```toml
    [well_known]
    ai_plugin_enabled = true        # /.well-known/ai-plugin.json (OpenAI manifest)
    ai_plugin_schema_version = "v1" # default; override for future spec versions
    # ai_plugin_name = "Acme Blog"  # defaults to site.title
    # ai_plugin_description = "..."  # defaults to site.description (omitted when both empty)

    security_txt_enabled = true     # /.well-known/security.txt (RFC 9116)
    security_txt_content = "Contact: mailto:security@example.com\nExpires: 2026-12-31T23:59:59.000Z\n"
    ```

    `ai-plugin.json` derives `name_for_human` from `ai_plugin_name` (default `site.title`), `name_for_model` from the slugified name, description from `ai_plugin_description` (default `site.description`), `logo_url` from `seo.org_logo` resolved against the base URL (omitted when unset), `auth: {type: "none"}` (static sites have no authenticated API), and `api: {type: "openapi", url: <base_url>/openapi.json}` — drop a `static/openapi.json` if you want a live contract. `security.txt` is written **verbatim** from `security_txt_content` (author supplies the full RFC 9116 text). When both flags are off, no `.well-known/` directory is created.

17. **Build:** `cstatic build`.

## Gotchas

- `base_url` is required for correct absolute URLs in feeds and OG tags; a missing/relative `base_url` produces broken absolute URLs.
- `rss_title` defaults to `site.title`; `rss_description` defaults to empty.
- JSON Feed items carry `content_html` (the rendered body). RSS and JSON Feed share `rss_title`/`rss_description`/`rss_item_count` so they stay in sync.
- Drafts and future-dated pages are excluded from feeds and the sitemap automatically.
- `sitemap.exclude` takes **URL paths** (e.g. `/404.html`), not file paths.
- `{{ seo_meta }}` values are XML-escaped; safe to insert raw into `<head>`.
- JSON-LD is **off by default** — existing builds are unchanged until you set `seo.json_ld_enabled = true`. The `BlogPosting` default is inferred only for URLs under `/posts/...`; use `type: Article` (etc.) in frontmatter to override.
- JSON-LD `page.schema` deep-merges: auto-generated fields (`headline`, `datePublished`, `author`, `image`, `url`...) are preserved; only the keys you list are overridden. Use `schema_extra` (array) when you need entirely separate top-level schemas (FAQ, Event, etc.).
- **FAQ authoring shortcut**: `##? question` headings anywhere in a page (no `{% schema %}` wrapper needed) auto-build a `FAQPage` — visible `<details>` HTML inline + a `FAQPage` JSON-LD merged into `schema_extra` + a `{{ page.faq }}` array. Wraps with `{% schema "FAQPage" %}` are still supported and merge into the same single `FAQPage`. Standalone FAQ is terminal (answers run to the next `##?` or end of body) — place it last on the page.
- **Author files are entities, not pages**: when `authors.enabled = true`, `src/authors/*.md` are loaded into the author index and **excluded** from the regular markdown page collection. Don't expect them to appear in `{{ pages }}` or render with the default template — they render via `templates/author.html` at `/<slug>/`.
- **Passage IDs and TOC IDs are aligned**: both G8 (passage index) and G11 (auto TOC) use the same shared `utils::slugify` to generate heading IDs. G11 injects matching `id="..."` attributes into rendered `<h2>`–`<h6>` tags so `#passage-id` anchor links and JSON-LD `hasPart[].url` targets resolve in browsers. Headings with pre-existing `id` attributes are preserved by G11 (but G8 still computes from text — rare edge case).
- **`json_ld_enabled` is under `[seo]`, not `[modules]`**: the scaffold's commented hint correctly sits under the `[seo]` table; the reader key is `seo.json_ld_enabled`. The `[authors]` table is separate (`authors.enabled`) and gates only the author entity system (resolution + profile pages), independent of JSON-LD. (Older scaffolds misplaced the comment under `[modules]` — uncommenting it there silently did nothing.)
- **Brand validation is always on when `org_name` is set** — no separate flag to enable/suppress. Warnings are non-fatal (print to stderr, don't fail the build). The `{{ site.org }}` variable (not top-level `{{ org }}`) is available in all templates when `org_name` is non-empty, regardless of `json_ld_enabled`.
- **`sitemap_ai` filters are cumulative** (all must pass): inherits `sitemap.exclude`, hardcodes `/tags/`+`/categories/`+`/page/` URL drops, requires `word_count > 100`, and checks `sitemap_ai.exclude_types`. The `word_count` gate naturally excludes taxonomy/listing pages (they lack `word_count`) and data-driven pages (G12 only runs on the markdown path). `include_images = true` (default) resolves relative image URLs to absolute via `site.base_url`; the `xmlns:image` namespace is only declared when at least one included page actually has images.
- **`.well-known` keys are flat under one table**: use `[well_known]` with bare keys (`ai_plugin_enabled`, `security_txt_enabled`, etc.) — the reader traverses `well_known.<key>` from the root, so a `[well_known]` header + bare keys works, as does root-level `well_known.ai_plugin_enabled = true`. `security_txt_content` is written verbatim (no templating/escaping); `ai-plugin.json`'s `api.url` always points at `<base_url>/openapi.json` regardless of whether that file exists (drop a `static/openapi.json` to make it real). `logo_url` rides on `seo.org_logo` — there is no separate `well_known` logo key.
