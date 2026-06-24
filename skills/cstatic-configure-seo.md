---
name: cstatic-configure-seo
description: Configure SEO and feeds for a C-Static site. Use when the user says "enable sitemap", "RSS feed", "JSON feed", "robots.txt", "OG images", "social cards", "seo_meta", or "SEO setup". Covers modules, {{ seo_meta }}, og_images, and sitemap excludes.
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
   modules.rss_title = "My Site Feed"
   modules.rss_description = "Latest posts from My Site"
   modules.rss_item_count = 20
   modules.json_feed_output = "feed.json"

   # robots.txt tuning:
   modules.robots_user_agent = "*"
   modules.robots_include_sitemap = true
   modules.robots_disallow = ["/admin/"]
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

7. **Build:** `cstatic build`.

## Gotchas

- `base_url` is required for correct absolute URLs in feeds and OG tags; a missing/relative `base_url` produces broken absolute URLs.
- `rss_title` defaults to `site.title`; `rss_description` defaults to empty.
- JSON Feed items carry `content_html` (the rendered body). RSS and JSON Feed share `rss_title`/`rss_description`/`rss_item_count` so they stay in sync.
- Drafts and future-dated pages are excluded from feeds and the sitemap automatically.
- `sitemap.exclude` takes **URL paths** (e.g. `/404.html`), not file paths.
- `{{ seo_meta }}` values are XML-escaped; safe to insert raw into `<head>`.
