# C-Static: 12-Feature Improvement Plan

## Context

C-Static has completed improvement plan v2 (phases 1–14) and now has solid core features: markdown rendering, templates, collections, taxonomies, data-driven pages, SEO meta, search index, asset pipeline, image optimization, and incremental builds. This plan adds 12 new features that close gaps with mature SSGs (Hugo/Zola/Eleventy) and introduce unique differentiators (OG image generation, backlinks, multi-format output). Features are listed in the user's requested priority order (OG images first), with a suggested implementation phasing at the end.

## Architecture Patterns to Reuse

- **Module pattern**: `void generate_X(const Config&, const nlohmann::json& pages, const std::string& output_dir)` — see `src/modules/rss.cpp`, `search.cpp`, `sitemap.cpp`
- **External tool detection**: `tool_available()` + `ToolCache` + `run_converter()` — `src/assets/asset_pipeline.cpp:130-166`
- **Config parsing**: `optional_string/optional_bool/optional_int` — `src/config/config.cpp:46-97`
- **CLI subcommand**: `app.add_subcommand()` + callback lambda — `src/main.cpp:28-51`
- **Frontmatter known_keys**: `src/content/frontmatter.cpp:124-127`
- **BuildError + source context**: `src/pipeline/builder.hpp:12-19`, `src/main.cpp:78-128`

---

## Feature 1: OG Image Generation ✅ DONE (PRIORITY #1)

**Approach**: SVG templates rendered with Inja, converted to PNG via external tools (rsvg-convert/convert/inkscape). Consistent with existing WebP/AVIF external tool pattern.

### Config — `src/config/config.hpp`
Add after `[build.search]` section (~line 50):
```cpp
// [og_images]
bool        og_images_enabled       = false;
std::string og_images_template      = "og-default";   // SVG template name (no .svg)
std::string og_images_output_format = "png";           // "png" or "svg"
int         og_images_width         = 1200;
int         og_images_height        = 630;
std::string og_images_output_dir    = "og";            // subdir under output/
```

### Config parsing — `src/config/config.cpp`
Parse with `optional_bool/string/int`. Add to `config_to_json()`.

### New files
- **`src/modules/og_images.hpp`** — `int generate_og_images(const Config&, nlohmann::json& pages, const std::string& output_dir, const std::string& template_dir)` (note: pages is mutable — og:image URLs are injected into it)
- **`src/modules/og_images.cpp`** — Implementation:
  1. Detect SVG→PNG converter (reuse `tool_available()` pattern from `asset_pipeline.cpp:130`): check `rsvg-convert`, `convert` (ImageMagick), `inkscape` in priority order. Cache result in static bool.
  2. Load SVG template from `templates/<og_images_template>.svg` using a standalone `inja::Environment` (not the main TemplateRenderer — avoids template dir conflicts).
  3. For each page with a non-empty title:
     - Build Inja context: `{ page: {title, date, excerpt, url}, site: {title, base_url} }`
     - Render SVG string
     - Write SVG to `output/<og_images_output_dir>/<slug>.svg`
     - If converter available + format is "png": write SVG to temp file, shell out to converter with width/height args, write PNG, clean temp
     - Set `page["og_image"] = "/<og_images_output_dir>/<slug>.<ext>"`
  4. Return count generated
  5. Slug derivation: strip leading/trailing `/` from URL, replace `/` with `-`

Converter commands:
- `rsvg-convert -w {W} -h {H} "{input}" -o "{output}"`
- `convert -density 150 "{input}" -resize {W}x{H} "{output}"`
- `inkscape --export-type-png --export-filename="{output}" "{input}"`

### Builder wiring — `src/pipeline/builder.cpp`
In modules section (after search module, ~line 1383), BEFORE sitemap/RSS so og:image URLs propagate:
```cpp
if (cfg.og_images_enabled) {
    modules::generate_og_images(cfg, pages_array, cfg.output_dir, cfg.template_dir);
}
```
Also update `build_seo_meta()` to check for `og_image` field as fallback when frontmatter `image` is empty.

### Scaffold — `src/main.cpp`
Add `templates/og-default.svg` to init scaffold `files[]` array (~line 409). Default SVG: 1200×630 with `{{ page.title }}` centered, `{{ site.title }}` in corner.

### Build system
- `CMakeLists.txt`: add `src/modules/og_images.cpp` to `add_executable(cstatic ...)`
- `tests/CMakeLists.txt`: add to `LIB_SOURCES`

### Tests — `tests/test_build_integration.cpp`
- SVG template renders with page context (no converter needed)
- Output SVG file at correct path
- Pages without titles are skipped
- `og_image` URL set in pages_array

### Docs
- `docs/config.md`: new `[og_images]` section
- `README.md`: features list + OG image mention

**Effort**: Medium | **Dependencies**: None

---

## Feature 2: Shortcodes / Content Components ✅ DONE

**Approach**: Pre-process `{{< name params >}}` syntax in markdown body before cmark-gfm rendering. Shortcode templates are Inja templates in `shortcodes/` directory. Since `CMARK_OPT_UNSAFE` is enabled (`markdown.cpp:487`), raw HTML from shortcodes passes through.

### Config — `src/config/config.hpp`
```cpp
std::string shortcodes_dir = "shortcodes";  // under project root
```
Parse as `build.markdown.shortcodes_dir`.

### New files
- **`src/content/shortcodes.hpp`** — `ShortcodeProcessor` class:
  ```cpp
  class ShortcodeProcessor {
  public:
      explicit ShortcodeProcessor(const std::string& shortcodes_dir);
      std::string process(const std::string& markdown, const nlohmann::json& page_context = {}) const;
  };
  ```
- **`src/content/shortcodes.cpp`** — Implementation:
  - **Parse**: Regex `\{\{<\s*(\w+)([^>]*?)\s*>\}\}` for opening, `\{\{<\s*/(\w+)\s*>\}\}` for closing
  - **Params**: Split middle group by whitespace. Positional args go into `params` array. `key="value"` pairs go into `named` object.
  - **Block shortcodes**: `{{< note >}}inner content{{< /note >}}` — capture inner content between open/close tags
  - **Render**: Load `shortcodes/<name>.html`, render with Inja context `{ params: [...], named: {...}, content: "...", page: {...} }`
  - **Cache**: Cache loaded shortcode templates in a mutable map

### Builder wiring — `src/pipeline/builder.cpp`
After config load (~line 458):
```cpp
ShortcodeProcessor shortcode_processor(cfg.shortcodes_dir);
```
In Phase 1 loop, before `render_markdown` (~line 539):
```cpp
rp.parsed.body = shortcode_processor.process(rp.parsed.body, page_context_json);
rp.html_content = render_markdown(rp.parsed.body, md_opts);
```

### Scaffold — `src/main.cpp`
Add `shortcodes/` directory creation to init. Add sample shortcodes: `shortcodes/youtube.html`, `shortcodes/figure.html`.

### Tests — `tests/test_shortcodes.cpp`
- Inline: `{{< youtube dQw4w9WgXcQ >}}` → iframe
- Named params: `{{< image src="x.jpg" alt="Alt" >}}`
- Block: `{{< note >}}text{{< /note >}}`
- Multiple shortcodes in one doc
- Unknown shortcode: warning + passthrough

**Effort**: Medium | **Dependencies**: None

---

## Feature 3: `cstatic new` Command ✅ DONE

### New files
- **`src/cli/content_generator.hpp`** / **`.cpp`**:
  ```cpp
  int generate_content(const std::string& path, const std::string& kind = "");
  ```
  - Load archetype from `archetypes/<kind>.md` (or `archetypes/default.md`)
  - Built-in default if no archetype file exists
  - Replace `{{ date }}` → today (YYYY-MM-DD), `{{ title }}` → title from filename, `{{ slug }}` → filename stem
  - Write to `<source_dir>/<path>`, creating parent dirs
  - Refuse to overwrite existing files

### CLI — `src/main.cpp`
New subcommand after `init`:
```cpp
auto* new_cmd = app.add_subcommand("new", "Create new content from archetype");
std::string new_path, new_kind;
new_cmd->add_option("path", new_path, "Content path (e.g. posts/my-post.md)")->required();
new_cmd->add_option("--kind", new_kind, "Archetype name");
new_cmd->callback([&]() { std::exit(cli::generate_content(new_path, new_kind)); });
```

### Scaffold
Add `archetypes/default.md` and `archetypes/post.md` to init.

### Tests — `tests/test_content_generator.cpp`
- Default archetype generation, custom kind, title derivation, overwrite protection

**Effort**: Small | **Dependencies**: None

---

## Feature 4: Scheduled Publishing

### Config — `src/config/config.hpp`
```cpp
bool publish_future = false;  // in [build] section
```

### Builder — `src/pipeline/builder.cpp`
After draft check (~line 517), add:
```cpp
if (!cfg.publish_future && !rp.parsed.frontmatter.date.empty()) {
    std::tm tm = {}; std::istringstream ss(rp.parsed.frontmatter.date);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (!ss.fail() && std::mktime(&tm) > std::time(nullptr)) {
        result.pages_skipped++; continue;
    }
}
```

### BuildResult — `src/pipeline/builder.hpp`
Add `int pages_scheduled = 0;` for accurate reporting (separate from drafts).

**Effort**: Small | **Dependencies**: None

---

## Feature 5: Broken Link Checker (`cstatic check`)

### New files
- **`src/pipeline/link_checker.hpp`**:
  ```cpp
  struct LinkIssue { std::string source_file; int line; std::string href; std::string message; bool is_external; };
  struct CheckResult { int total_links; int internal_checked; int external_checked; std::vector<LinkIssue> issues; };
  CheckResult check_links(const std::string& output_dir, bool check_external = false, int timeout_ms = 5000);
  ```
- **`src/pipeline/link_checker.cpp`**:
  - Scan all `.html` files in output_dir
  - Regex extract `href="..."` and `src="..."`
  - Internal links (starting `/`): resolve to file path, check existence. Strip `#fragment` first. Directory URLs → check `index.html`.
  - External links (optional, `--external` flag): HTTP HEAD via `cpp-httplib` (already a dependency), follow 3 redirects, mark non-200
  - Track line numbers by counting newlines to match position

### CLI — `src/main.cpp`
```cpp
auto* check_cmd = app.add_subcommand("check", "Check for broken links");
bool check_external = false;
check_cmd->add_flag("--external", check_external, "Also verify external links");
check_cmd->callback([&]() { /* load config, run check_links, print results, exit code */ });
```

### Config
```cpp
bool check_external = false; int check_timeout_ms = 5000;  // [check] section
```

**Effort**: Medium | **Dependencies**: None

---

## Feature 6: Backlinks / Content Relationships

### Config — `src/config/config.hpp`
```cpp
bool wikilinks_enabled = false;  // in [build.markdown]
```

### New files
- **`src/content/link_graph.hpp`**:
  ```cpp
  struct Wikilink { std::string target; std::string display; };
  class LinkGraph {
      void add_page(const std::string& url, const std::string& title, const std::vector<Wikilink>& outgoing);
      void resolve(const std::map<std::string,std::string>& slug_to_url);
      nlohmann::json get_backlinks(const std::string& url) const;
  };
  std::vector<Wikilink> parse_wikilinks(std::string& markdown);  // rewrites [[x]] to HTML <a>
  ```
- **`src/content/link_graph.cpp`**:
  - Regex `\[\[([^\]|]+)(?:\|([^\]]+))?\]\]` for wikilinks
  - Replace with `<a href="/resolved/url">display</a>` (raw HTML, survives cmark-gfm)
  - Slug matching: lowercase title, spaces→hyphens. Also match by URL slug.
  - Backlinks: reverse graph — for each page, list pages linking TO it

### Builder — `src/pipeline/builder.cpp`
- Add `std::vector<Wikilink> outgoing_links` to `RawPage` struct
- After Phase 1: build LinkGraph, resolve slugs
- In Phase 2 render context: `ctx["page"]["backlinks"] = link_graph.get_backlinks(rp.url);`

**Effort**: Medium | **Dependencies**: None

---

## Feature 7: Multi-format Output

### Config — `src/config/config.hpp`
```cpp
struct OutputFormat { std::string format; std::string output_dir; std::string extension; };
std::vector<OutputFormat> output_formats;  // [[output]] array of tables
```

### New files
- **`src/modules/gemtext.hpp`** / **`.cpp`**: HTML→Gemtext converter
  - `<h1-3>` → `#`/`##`/`###`
  - `<a href="u">t</a>` → `=> u t`
  - `<ul><li>` → `* item`
  - `<pre><code>` → fenced ```
  - Paragraphs → blank-line separated text
  - Strip all other tags (reuse `utils::strip_html_tags` as final pass)

### Builder — `src/pipeline/builder.cpp`
In modules section:
```cpp
for (const auto& fmt : cfg.output_formats) {
    if (fmt.format == "gemtext") modules::generate_gemtext(pages_array, cfg.output_dir + "/" + fmt.output_dir);
}
```

**Effort**: Medium | **Dependencies**: None

---

## Feature 8: Incremental Dev Server Rebuilds

**Problem**: `dev_server.cpp:470` calls `build_site(cfg, false, ...)` — `false` means full rebuild every time. Additionally, when pages are added/deleted, the `{{ pages }}` context in all templates becomes stale because the incremental hash only tracks individual files, not the pages_array.

### Changes

**`src/server/dev_server.cpp`** — line 470:
```cpp
// Change from:
auto result = build_site(cfg, false, include_drafts_);
// To:
auto result = build_site(cfg, cfg.incremental_enabled, include_drafts_);
```

**`src/pipeline/builder.cpp`** — hash the pages_array so structural changes invalidate all pages:
1. After Phase 1 completes (~line 627): `hashes.hash_string("meta:pages_array", pages_array.dump());`
2. In Phase 2 dependency list (~line 1010): add `"meta:pages_array"` to every page's deps
3. This ensures adding/deleting a page triggers rebuild of all pages that reference `{{ pages }}`

**Fallback safety** in `dev_server.cpp`: if incremental rebuild produces 0 pages built + 0 cached with no errors (suspicious), retry as full rebuild.

**Effort**: Small-Medium | **Dependencies**: None

---

## Feature 9: Template Inheritance / Layouts

**Approach**: Pre-process templates to implement Jinja2-style `{% extends "base" %}` + `{% block name %}`. This happens BEFORE Inja rendering.

### Renderer — `src/template/renderer.cpp`
Add private methods to `TemplateRenderer`:
```cpp
std::string resolve_inheritance(const std::string& tmpl) const;
std::map<std::string,std::string> extract_blocks(const std::string& tmpl) const;
std::string apply_blocks(const std::string& parent, const std::map<std::string,std::string>& blocks) const;
```

In `load_template()` (~line 66), call `resolve_inheritance()` on loaded template before caching.

**Logic**:
1. Regex `\{%\s*extends\s+["']([^"']+)["']\s*%\}` — if found, load parent template recursively
2. Extract blocks: `\{%\s*block\s+(\w+)\s*%\}([\s\S]*?)\{%\s*endblock\s*%\}`
3. In parent, replace matching blocks with child content; keep parent default if child doesn't define
4. Support multi-level inheritance (A extends B extends C)

### Tests — `tests/test_template_renderer.cpp`
- Simple extends, multiple blocks, default content preserved, nested extends, no extends = passthrough

**Effort**: Medium | **Dependencies**: None

---

## Feature 10: JSON Feed

### Config — `src/config/config.hpp`
```cpp
bool module_json_feed = false;  // in [modules]
```

### New files
- **`src/modules/json_feed.hpp`** / **`.cpp`**: Mirror `rss.cpp` structure exactly. JSON Feed 1.1 spec:
  ```json
  {"version":"https://jsonfeed.org/version/1.1","title":"...","home_page_url":"...","feed_url":".../feed.json",
   "items":[{"id":"...","url":"...","title":"...","content_html":"...","date_published":"...","summary":"..."}]}
  ```
  Reuse `cfg.rss_item_count` for entry limit. Use `nlohmann::json` for serialization (auto-escaping).

### Builder — `src/pipeline/builder.cpp`
```cpp
if (cfg.module_json_feed) modules::generate_json_feed(cfg, pages_array, cfg.output_dir);
```

**Effort**: Small | **Dependencies**: None

---

## Feature 11: `--watch` for Build Mode

### New files
- **`src/server/file_watcher.hpp`** / **`.cpp`**: Extract platform-specific watching code from `dev_server.cpp:265-465` into reusable class:
  ```cpp
  class FileWatcher {
      using Callback = std::function<void()>;
      FileWatcher(const std::vector<std::string>& dirs, Callback cb, int debounce_ms = 150);
      void start(); // blocking
      void stop();
  };
  ```
  DevServer internally uses FileWatcher (refactor existing code).

### CLI — `src/main.cpp`
Add `--watch` flag to build subcommand:
```cpp
bool watch = false;
build_cmd->add_flag("--watch", watch, "Rebuild on file changes");
```
When set, after initial build, start FileWatcher with rebuild callback (no HTTP server). Handle SIGINT for clean exit.

**Effort**: Small-Medium | **Dependencies**: Benefits from Feature 8

---

## Feature 12: Better Error Rendering

### BuildError — `src/pipeline/builder.hpp`
Add `int column = 0;` field for column-level precision.

### Frontmatter errors — `src/content/frontmatter.cpp`
YAML parser exceptions from yaml-cpp carry `e.mark.line` and `e.mark.column`. Capture these in a custom exception:
```cpp
class FrontmatterError : public std::runtime_error {
    int line_, column_;
    // ...
};
```
Throw this instead of generic `std::runtime_error`, then catch in builder.cpp to populate BuildError with line/column.

### Error display — `src/main.cpp`
Enhance `format_build_error()` (lines 78-128) to show source context for markdown/frontmatter errors (currently only template errors get context). Read the source file, show ±3 lines with `>` marker on the error line, caret at column position.

Add `--verbose` / `-v` flag to build subcommand for additional diagnostic output.

**Effort**: Small | **Dependencies**: None

---

## Suggested Implementation Phasing

While features are listed above with OG images as #1, the optimal build order groups by dependencies and shared patterns:

| Phase | Features | Rationale |
|-------|----------|-----------|
| **A: Quick Wins** | #4 Scheduled, #10 JSON Feed, #12 Better Errors | Small, self-contained, immediate DX value |
| **B: Content Workflow** | #2 Shortcodes, #3 `cstatic new`, #9 Template Inheritance | Core authoring improvements; inheritance benefits shortcode templates |
| **C: OG Images** ✅ | #1 OG Image Generation | The headline feature; benefits from stable pipeline |
| **D: Content Intelligence** | #6 Backlinks, #5 Link Checker | Both analyze content relationships |
| **E: Infrastructure** | #8 Incremental Dev, #11 `--watch`, #7 Multi-format | Performance and output flexibility |

Each phase can be committed independently. Features within a phase can be parallelized.

## Critical Files

| File | Features touching it |
|------|---------------------|
| `src/pipeline/builder.cpp` | 1, 2, 4, 6, 7, 8, 10, 12 |
| `src/config/config.hpp` + `.cpp` | 1, 2, 4, 5, 6, 7, 10 |
| `src/main.cpp` | 1, 3, 5, 11, 12 |
| `src/template/renderer.cpp` | 9 |
| `src/content/frontmatter.cpp` | 12 |
| `src/server/dev_server.cpp` | 8, 11 |
| `CMakeLists.txt` + `tests/CMakeLists.txt` | All new source files |

## Verification

After each feature:
1. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCSTATIC_BUILD_TESTS=ON && cmake --build build -j$(sysctl -n hw.ncpu)`
2. `cd build && ctest --output-on-failure` — all tests must pass
3. Manual smoke test with scaffold: `./build/cstatic init /tmp/test && ./build/cstatic build /tmp/test`
4. For OG images specifically: verify SVG output exists, verify PNG output if converter available (`which rsvg-convert`), verify `og:image` URL in rendered HTML
