# C-Static Improvement Plan v2

## Context

Phases 1–6 of the original improvement plan are complete. This plan covers the next round of features to bring C-Static closer to feature parity with Hugo, Zola, 11ty, and Jekyll. 14 features are organized into 7 phases, ordered by dependency and impact. Each phase is independently buildable, testable, and committable.

---

## Phase 7: Template Partials & HTML Minification ✅ DONE

Two high-impact, low-risk features that share the renderer/pipeline layer.

### 7A. Template Partials (Includes) ✅

- **Files**: `src/template/renderer.hpp`, `src/template/renderer.cpp`
- Register an Inja include callback so `{% include "nav.html" %}` resolves against `templates/` directory
- Inja natively supports `include` statements — wire `env_.set_include_callback()` to load from `template_dir_/partials/` (fall back to `template_dir_/`)
- Pre-load all `.html` files in `templates/partials/` during `TemplateRenderer` construction so they're in cache for multi-threaded rendering
- Add `templates/partials/nav.html` to the `cstatic init` scaffold in `src/main.cpp`
- **Test**: create a template that includes a partial, verify rendered output contains the partial content

### 7B. HTML Minification ✅

- **Files**: `src/assets/asset_pipeline.hpp`, `src/assets/asset_pipeline.cpp`, `src/config/config.hpp`, `src/config/config.cpp`, `tests/test_minifier.cpp`
- Add `minify_html()` to asset_pipeline — state machine that:
  - Collapses whitespace between block-level tags to a single space
  - Removes HTML comments (`<!-- -->`) unless they start with `<!--[if`
  - Removes optional closing tags (`</li>`, `</p>`, `</dt>`, etc.)
  - Removes quotes on attribute values where safe (all-numeric or single-word)
- Add `bool minify_html` to config (default `true`, under `[build.minify]`)
- Apply to all generated HTML outputs in `builder.cpp` (both markdown pages and data-driven pages) after rendering, before writing to disk
- **Test**: comments removed, whitespace collapsed, optional close tags stripped, conditional comments preserved

### 7C. Update Config Docs ✅

- **Files**: `docs/config.md`, `README.md`
- Document `build.minify.html` option
- Document `{% include "..." %}` partial syntax in README template section
- Document `templates/partials/` convention

**Verify**: `cstatic build` with a site using includes produces correct output. HTML output is minified. All existing tests pass.

---

## Phase 8: Content Collections ✅ DONE

Define reusable content types with default templates, URL patterns, and sort order — the core organizational feature in Hugo/Zola.

### 8A. Collection Config ✅

- **Files**: `src/config/config.hpp`, `src/config/config.cpp`
- Add to config.hpp:
  ```cpp
  struct Collection {
      std::string name;           // "posts" — matches src/posts/ directory
      std::string template_;      // "post" — default layout for items
      std::string index_template; // "posts" — layout for index pages (default: name + "-index")
      std::string url_pattern;    // "/posts/{{ slug }}/" (default: "/name/slug/")
      std::string sort_by = "date";       // field to sort by
      std::string sort_order = "desc";    // "desc" or "asc"
  };
  std::vector<Collection> collections;
  ```
- Parse `[[collection]]` TOML array of tables in config.cpp
- Add to `config_to_json()` serialization

### 8B. Collection Rendering ✅

- **Files**: `src/pipeline/builder.cpp`, `src/pipeline/builder.hpp`, `tests/test_build_integration.cpp`
- After Phase 1 markdown parsing, match each `RawPage` to a collection by checking if its source path starts with `src/<collection.name>/`
- For matched pages:
  - Apply the collection's default template if no `layout` is set in frontmatter
  - Use the collection's URL pattern for permalink generation (interpolate `{{ slug }}` from the filename stem)
  - Sort by `sort_by` field in the specified order
- After all collection pages are parsed, generate index pages (one listing page per collection with all its pages, optionally paginated if `per_page` is set)
- Add `collections` to template context: `{{ collections.posts }}` returns all pages in the "posts" collection, sorted and filtered
- **Test**: create a `posts` collection with 3 markdown files, verify templates are applied and pages appear in `collections.posts`

### 8C. Update Scaffold & Docs ✅

- **Files**: `src/main.cpp`, `docs/config.md`, `README.md`
- Add `[[collection]]` example to scaffold `config.toml` (posts collection)
- Create scaffold `src/posts/first-post.md`
- Create scaffold `templates/post.html` (single post layout)
- Create scaffold `templates/posts-index.html` (post listing)
- Document `[[collection]]` config in docs/config.md

**Verify**: `cstatic init` → `cstatic build` with collections produces correct per-collection templates and index pages.

---

## Phase 9: Taxonomy Pages ✅ DONE

Automatic tag and category index pages — `/tags/webdev/`, `/categories/tutorials/`, etc.

### 9A. Taxonomy Config & Data Model ✅

- **Files**: `src/config/config.hpp`, `src/config/config.cpp`
- Add to config.hpp:
  ```cpp
  struct Taxonomy {
      std::string key;            // "tags" — frontmatter field to index
      std::string template_;      // "tag" — template for term pages
      std::string index_template; // "tags" — template for the taxonomy index page
  };
  std::vector<Taxonomy> taxonomies;
  ```
- Parse `[[taxonomy]]` TOML array of tables in config.cpp

### 9B. Taxonomy Page Generation ✅

- **Files**: `src/pipeline/builder.cpp`, `tests/test_build_integration.cpp`
- After Phase 1, build a `std::map<std::string, std::vector<page_ref>>` for each taxonomy by extracting the frontmatter field (e.g., `tags`) from each page
- Generate two types of pages per taxonomy:
  1. **Index page** at `/<key>/` (e.g., `/tags/`) — lists all terms with counts
  2. **Term pages** at `/<key>/<term>/` (e.g., `/tags/webdev/`) — lists all pages with that term
- Template context for term pages: `{{ taxonomy.key }}`, `{{ taxonomy.term }}`, `{{ taxonomy.pages }}`
- Template context for index page: `{{ taxonomy.key }}`, `{{ taxonomy.terms }}` (array of `{term, count, url}`)
- Add term pages to `pages_array` for sitemap inclusion
- **Test**: 3 pages with tags, verify `/tags/` index and `/tags/webdev/` listing exist with correct content

### 9C. Update Docs ✅

- **Files**: `docs/config.md`, `README.md`
- Document `[[taxonomy]]` config
- Document taxonomy template context variables
- Add taxonomy example to scaffold (tags taxonomy with tag.html template)

**Verify**: Build a site with tags, verify tag index and per-tag pages are generated and linked from sitemap.

---

## Phase 10: Image Optimization & Asset Fingerprinting ✅ DONE

### 10A. Image Optimization

- **Files**: `src/assets/asset_pipeline.hpp`, `src/assets/asset_pipeline.cpp`, `src/config/config.hpp`, `src/config/config.cpp`
- Add image optimization config:
  ```toml
  [build.images]
  optimize = true       # enable image processing
  max_width = 1920      # resize images wider than this
  quality = 85          # JPEG quality (1-100)
  webp = true           # convert JPEG/PNG to WebP
  ```
- In asset_pipeline, when processing image files (`.jpg`, `.jpeg`, `.png`, `.gif`, `.webp`):
  - Use `stb_image.h` (header-only, add via FetchContent) for loading
  - Use `stb_image_write.h` for output
  - If `webp = true`: produce `.webp` copies alongside originals
  - If `max_width > 0` and image exceeds it: resize proportionally
  - Apply JPEG quality setting
  - Report bytes saved in `AssetResult`
- Skip SVG files (pass through as-is)
- Add both original and WebP versions to output, so templates can use `<picture>` tags

### 10B. Asset Fingerprinting

- **Files**: `src/assets/asset_pipeline.hpp`, `src/assets/asset_pipeline.cpp`, `src/pipeline/builder.cpp`, `src/template/renderer.hpp`, `src/template/renderer.cpp`
- Add `bool fingerprint_assets` to config (default `false`, under `[build]`)
- When enabled, append content hash to asset filenames: `style.css` → `style.a3f7b2.css`
- Build a manifest map: `{"css/style.css": "css/style.a3f7b2.css", ...}`
- Write manifest to `output/manifest.json`
- Expose manifest in template context as `{{ assets["css/style.css"] }}`
- Add a template helper `{{ asset("css/style.css") }}` that resolves to the fingerprinted path

### 10C. Update Docs & Tests

- **Files**: `docs/config.md`, `README.md`, `tests/test_minifier.cpp`
- Document `[build.images]` and `[build.fingerprint_assets]`
- Document `{{ asset() }}` template function and `manifest.json`
- Add unit tests for image resizing and fingerprint hash generation

**Verify**: Build a site with images, verify WebP copies are generated. With fingerprinting enabled, verify `manifest.json` is correct and `{{ asset() }}` resolves in templates.

---

## Phase 11: Syntax Highlighting & Markdown Extensions ✅ DONE

### 11A. Syntax Highlighting

- **Files**: `src/content/markdown.hpp`, `src/content/markdown.cpp`, `src/config/config.hpp`, `src/config/config.cpp`
- Add config:
  ```toml
  [build.highlight]
  enabled = true
  style = "github"       # CSS theme name
  ```
- Integrate `tree-sitter` via CMake FetchContent (or use a simpler approach: add CSS classes via regex for common languages and include a pre-built highlight.js CSS file)
- Simpler alternative (recommended for C++): post-process rendered HTML code blocks (`<pre><code class="language-X">`) to wrap tokens in `<span class="hl-*>">` classes using a lightweight regex tokenizer for the top 10 languages (JS, Python, C++, Go, Rust, HTML, CSS, JSON, YAML, Bash)
- Generate `output/css/highlight.css` from the selected style
- Add `highlight` to the scaffold's default template

### 11B. Markdown Extensions

- **Files**: `src/content/markdown.cpp`, `src/config/config.hpp`, `src/config/config.cpp`, `tests/test_markdown.cpp`
- cmark already supports these via extensions — add `cmark-gfm` (GitHub-Flavored Markdown) as a FetchContent dependency to replace `cmark`
- This adds: tables, task lists (`- [x]`), strikethrough (`~~text~~`), autolinks
- Add config:
  ```toml
  [build.markdown]
  extensions = ["table", "tasklist", "strikethrough", "autolink"]
  ```
- **Test**: verify tables, task lists, strikethrough render correctly

### 11C. Update Docs

- Document syntax highlighting config and supported languages
- Document markdown extensions

**Verify**: Markdown with fenced code blocks, tables, and task lists renders correctly with syntax highlighting.

---

## Phase 12: Build Profiles & Page Aliases ✅ DONE

### 12A. Environment / Build Profiles ✅

- **Files**: `src/config/config.hpp`, `src/config/config.cpp`, `src/main.cpp`, `src/pipeline/builder.hpp`, `src/pipeline/builder.cpp`
- Add `--env` / `-e` flag to `build` and `serve` commands: `cstatic build --env production`
- Config overlay system: if `config.<env>.toml` exists (e.g., `config.production.toml`), merge its values on top of the base `config.toml`
- Merge strategy: for each key in the overlay, replace the base value. Arrays are replaced, not concatenated
- Always load `config.toml` first, then overlay the environment-specific config
- Template context gets `{{ site.env }}` = current environment name (default: `"development"`)

### 12B. Page Aliases / Redirects ✅

- **Files**: `src/content/frontmatter.hpp`, `src/content/frontmatter.cpp`, `src/pipeline/builder.cpp`, `tests/test_build_integration.cpp`
- Add `std::vector<std::string> aliases` to `Frontmatter` struct
- Parse `aliases` YAML array in frontmatter:
  ```yaml
  ---
  title: New Location
  aliases:
    - /old-url/
    - /another/old/path/
  ---
  ```
- In builder.cpp, after all pages are rendered, generate redirect HTML for each alias:
  ```html
  <!DOCTYPE html>
  <html>
  <head><meta http-equiv="refresh" content="0; url=/new-location/"></head>
  <body>Redirecting to <a href="/new-location/">New Location</a>.</body>
  </html>
  ```
- Write redirect pages to the alias URLs' output paths
- Add redirect pages to sitemap with `<lastmod>` but no extra weight
- **Test**: page with aliases generates redirect files at alias paths

### 12C. Update Docs ✅

- Document `--env` flag and `config.<env>.toml` overlay system
- Document `aliases` frontmatter field
- Document `{{ site.env }}` template variable

**Verify**: `cstatic build --env production` with a `config.production.toml` overlay uses production base_url. Aliases generate redirect pages.

---

## Phase 13: Error Reporting & Plugin Hooks ✅ DONE

### 13A. Better Error Reporting

- **Files**: `src/template/renderer.cpp`, `src/content/frontmatter.cpp`, `src/content/markdown.cpp`, `src/pipeline/builder.cpp`
- Template errors: wrap Inja exceptions with:
  - Source file path (which `.md` file was being rendered)
  - Template name and file path
  - The failing line of template code, with context (±3 lines)
  - The frontmatter `title` of the page that triggered the error
- Frontmatter errors: include filename, line number of the YAML parse error, and a snippet of the problematic YAML
- Build summary: at the end of a failed build, print a structured error summary:
  ```
  error: Build failed with 2 error(s)

  1. src/posts/hello.md: template error in 'post'
     templates/post.html line 14: undefined variable 'page.author'
     12 | <span class="author">{{ page.author }}</span>
     13 |                                  ^

  2. src/about.md: invalid frontmatter
     line 5: expected string, got integer for key 'date'
  ```
- Use a `BuildError` struct to collect errors during build instead of throwing on first error (continue processing other pages, then report all errors at once)

### 13B. Plugin Hook System

- **Files**: `src/pipeline/builder.hpp`, `src/pipeline/builder.cpp`, `src/config/config.hpp`, `src/config/config.cpp`
- Add config for hook scripts:
  ```toml
  [hooks]
  before_build = "scripts/pre-build.sh"   # run before any processing
  after_build = "scripts/post-build.sh"   # run after all output is written
  ```
- Hook scripts receive environment variables: `CSTATIC_ENV`, `CSTATIC_OUTPUT_DIR`, `CSTATIC_PAGES_BUILT`
- Execute via `std::system()` with error checking
- Print hook output to stdout/stderr
- If a hook exits non-zero, abort the build with a clear error message
- Config validation: warn if hook script path doesn't exist (don't error — it may be environment-specific)

### 13C. Update Docs

- Document error reporting format and `--continue-on-error` behavior
- Document hook scripts and available environment variables
- Add example hook scripts to scaffold (disabled by default)

**Verify**: Intentional template error shows structured output with file/line/snippet. Hook script runs before and after build.

---

## Phase 14: SEO Helpers & Search Index ✅ DONE

### 14A. Auto Open Graph / SEO Meta Tags

- **Files**: `src/pipeline/builder.cpp`, `src/content/frontmatter.hpp`, `src/content/frontmatter.cpp`
- Add known frontmatter fields: `description`, `image`, `canonical`
- If frontmatter has `description`, use it; otherwise fall back to the 200-char `excerpt`
- If frontmatter has `image`, treat it as the OG image URL
- Add a `{{ seo_meta }}` variable to page context that contains a pre-rendered HTML string:
  ```html
  <meta name="description" content="...">
  <meta property="og:title" content="...">
  <meta property="og:description" content="...">
  <meta property="og:url" content="...">
  <meta property="og:image" content="...">
  <meta name="twitter:card" content="summary">
  <link rel="canonical" href="...">
  ```
- Templates just need `{{ seo_meta }}` in `<head>`
- Add `seo_meta` to the scaffold's default template
- Config additions:
  ```toml
  [site]
  twitter_handle = "@username"    # for twitter:site meta
  ```

### 14B. Search Index Generation

- **Files**: `src/modules/search.hpp`, `src/modules/search.cpp`, `src/config/config.hpp`, `src/config/config.cpp`, `src/pipeline/builder.cpp`
- Add config:
  ```toml
  [build.search]
  enabled = true
  output = "search-index.json"     # output filename
  ```
- New module: `generate_search_index(cfg, pages_array, output_dir)`
- Produces a JSON file: `[{title, url, excerpt, date, tags}, ...]`
- Add to builder.cpp module generation phase (after sitemap/RSS)
- Consumable by client-side libraries: Lunr.js, Pagefind, Fuse.js
- **Test**: build with search enabled, verify `search-index.json` contains all non-draft pages

### 14C. Sitemap Enhancements

- **Files**: `src/modules/sitemap.cpp`, `src/content/frontmatter.hpp`
- Add `changefreq` and `priority` to frontmatter:
  ```yaml
  ---
  sitemap_changefreq: monthly
  sitemap_priority: 0.8
  ---
  ```
- Add `sitemap_changefreq` and `sitemap_priority` to `pages_array` entries in builder.cpp
- In sitemap.cpp: emit `<changefreq>` and `<priority>` elements when present
- **Test**: page with sitemap options produces correct XML elements

### 14D. Update Docs

- Document `description`, `image`, `canonical`, `sitemap_changefreq`, `sitemap_priority` frontmatter fields
- Document `{{ seo_meta }}` template variable
- Document `[build.search]` config and `search-index.json` format
- Document sitemap enhancement fields

**Verify**: Build a site with SEO frontmatter, verify OG meta tags in output, `search-index.json` is generated, and sitemap includes changefreq/priority.

---

## File Change Summary

| Phase | New Files | Modified Files |
|---|---|---|
| 7A | — | `renderer.hpp`, `renderer.cpp`, `main.cpp` |
| 7B | — | `asset_pipeline.hpp`, `asset_pipeline.cpp`, `config.hpp`, `config.cpp`, `builder.cpp`, `test_minifier.cpp` |
| 7C | — | `docs/config.md`, `README.md` |
| 8A | — | `config.hpp`, `config.cpp` |
| 8B | — | `builder.cpp`, `builder.hpp`, `test_build_integration.cpp` |
| 8C | — | `main.cpp`, `docs/config.md`, `README.md` |
| 9A | — | `config.hpp`, `config.cpp` |
| 9B | — | `builder.cpp`, `test_build_integration.cpp` |
| 9C | — | `docs/config.md`, `README.md` |
| 10A | — | `asset_pipeline.hpp`, `asset_pipeline.cpp`, `config.hpp`, `config.cpp` |
| 10B | — | `asset_pipeline.hpp`, `asset_pipeline.cpp`, `builder.cpp`, `renderer.hpp`, `renderer.cpp` |
| 10C | — | `docs/config.md`, `README.md`, `test_minifier.cpp` |
| 11A | — | `markdown.hpp`, `markdown.cpp`, `config.hpp`, `config.cpp` |
| 11B | — | `markdown.cpp`, `config.hpp`, `config.cpp`, `test_markdown.cpp` |
| 11C | — | `docs/config.md`, `README.md` |
| 12A | — | `config.hpp`, `config.cpp`, `main.cpp`, `builder.hpp`, `builder.cpp` |
| 12B | — | `frontmatter.hpp`, `frontmatter.cpp`, `builder.cpp`, `test_build_integration.cpp` |
| 12C | — | `docs/config.md`, `README.md` |
| 13A | — | `renderer.cpp`, `frontmatter.cpp`, `markdown.cpp`, `builder.cpp`, `builder.hpp` |
| 13B | — | `config.hpp`, `config.cpp`, `builder.cpp` |
| 13C | — | `docs/config.md`, `README.md` |
| 14A | `src/modules/search.hpp`, `src/modules/search.cpp` | `builder.cpp`, `frontmatter.hpp`, `frontmatter.cpp`, `config.hpp`, `config.cpp`, `main.cpp` |
| 14B | — | `builder.cpp`, `config.hpp`, `config.cpp` |
| 14C | — | `sitemap.cpp`, `frontmatter.hpp`, `builder.cpp` |
| 14D | — | `docs/config.md`, `README.md` |

**Most modified files**: `builder.cpp` (phases 7–14), `config.hpp`/`config.cpp` (phases 7–14), `docs/config.md` (every phase), `README.md` (every phase)

---

## Verification

After each phase:
1. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCSTATIC_BUILD_TESTS=ON && cmake --build build -j$(nproc)`
2. `cd build && ctest --output-on-failure`
3. Manual smoke test: `cstatic init` in a temp dir, `cstatic build`, `cstatic serve`

After all phases:
4. Verify CI passes on all 3 platforms (macOS, Linux, Windows)
5. Verify `cstatic build --env production` with overlay config
6. Verify taxonomy pages generate correct tag/category indexes
7. Verify image pipeline produces WebP copies and fingerprinted assets
8. Verify search-index.json contains all published pages
9. Verify syntax highlighting renders in code blocks
10. Verify error output shows structured multi-error report
