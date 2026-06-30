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

8. **Build:** `cstatic build`.

## Gotchas

- `base_url` is required for correct absolute URLs in feeds and OG tags; a missing/relative `base_url` produces broken absolute URLs.
- `rss_title` defaults to `site.title`; `rss_description` defaults to empty.
- JSON Feed items carry `content_html` (the rendered body). RSS and JSON Feed share `rss_title`/`rss_description`/`rss_item_count` so they stay in sync.
- Drafts and future-dated pages are excluded from feeds and the sitemap automatically.
- `sitemap.exclude` takes **URL paths** (e.g. `/404.html`), not file paths.
- `{{ seo_meta }}` values are XML-escaped; safe to insert raw into `<head>`.
- JSON-LD is **off by default** — existing builds are unchanged until you set `seo.json_ld_enabled = true`. The `BlogPosting` default is inferred only for URLs under `/posts/...`; use `type: Article` (etc.) in frontmatter to override.
- JSON-LD `page.schema` deep-merges: auto-generated fields (`headline`, `datePublished`, `author`, `image`, `url`...) are preserved; only the keys you list are overridden. Use `schema_extra` (array) when you need entirely separate top-level schemas (FAQ, Event, etc.).
