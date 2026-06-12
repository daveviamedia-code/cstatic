# C-Static v0.2.0 Improvement Plan

## Context

A code audit of C-Static v0.2.0 identified 21 issues across correctness, performance, portability, test coverage, and documentation. This plan organizes all fixes into 6 phases that are independently buildable, testable, and committable. Earlier phases lay groundwork for later ones (e.g., centralizing duplicate utilities before adding features that depend on them).

---

## Phase 1: Trivial Fixes & Documentation — DONE

### 1a. Fix RSS output path in README
- **File**: `README.md` line 193
- Change `/rss.xml` to `/feed.xml` to match `src/modules/rss.cpp:104` (the code is correct, README is wrong)

### 1b. Fix scaffold placeholder URLs
- **File**: `src/main.cpp` lines 79 and 153
- Replace `https://github.com/your-org/c-static` with `https://github.com/daveviamedia-code/cstatic`

### 1c. Create docs/config.md
- **New file**: `docs/config.md`
- Document every config key from `src/config/config.hpp` (site, build, incremental, minify, modules, rss, robots, sitemap, data, data_source)
- Include types, defaults, and examples

**Verify**: `cstatic init` scaffold has correct URLs. README link to docs/config.md resolves. `cstatic build` works.

---

## Phase 2: Centralize Duplicate Utilities — DONE

### Duplicated functions to consolidate

| Function | Current locations | Notes |
|---|---|---|
| `read_file()` text | builder.cpp:30, dev_server.cpp:75, config.cpp:15 | config.cpp's throws `ConfigError` — leave it alone |
| `read_file_binary()` | asset_pipeline.cpp:19 | Binary mode |
| `write_file()` text | builder.cpp:42, sitemap.cpp:14, rss.cpp:16, robots.cpp:13 | All include mkdir logic |
| `write_file_binary()` | asset_pipeline.cpp:31 | Binary mode |
| `xml_escape()` | sitemap.cpp:57, rss.cpp:29 | Identical implementations |

### Steps
1. Create `src/utils/file_io.hpp` declaring: `read_file()`, `read_file_binary()`, `write_file()`, `write_file_binary()`, `xml_escape()` in `namespace cstatic::utils`
2. Create `src/utils/file_io.cpp` with implementations (use the most robust variant from each site — merge builder.cpp's mkdir + error labeling with sitemap.cpp's `fs::path::parent_path()`)
3. Add `src/utils/file_io.cpp` to `CMakeLists.txt` main executable source list and `tests/CMakeLists.txt` LIB_SOURCES
4. Remove local `static` definitions and add `#include "utils/file_io.hpp"` in:
   - `src/pipeline/builder.cpp`
   - `src/assets/asset_pipeline.cpp`
   - `src/modules/sitemap.cpp`
   - `src/modules/rss.cpp`
   - `src/modules/robots.cpp`
5. For `src/server/dev_server.cpp`: the local `read_file` returns empty on failure (doesn't throw). Wrap the shared `read_file()` in try-catch at call sites, or add a `read_file_or_empty()` variant

**Verify**: Full test suite passes. `cstatic build` and `cstatic serve` work. 404 serving, RSS generation, file writing all functional.

---

## Phase 3: Core Correctness (5 sub-phases, independently committable) — DONE

### 3A. Cache Inja Environment
- **Files**: `src/template/renderer.hpp`, `src/template/renderer.cpp`
- Add `inja::Environment env_` as class member (initialized in constructor)
- Add `std::unordered_map<std::string, std::string> template_cache_` for loaded templates
- `render()`: use member `env_`, check cache before loading from disk
- Add `preload_template(const std::string& name)` public method for pre-loading (needed by Phase 4B)

### 3B. Non-Scalar Frontmatter Fields
- **Files**: `src/content/frontmatter.hpp`, `src/content/frontmatter.cpp`, `src/pipeline/builder.cpp`, `tests/test_frontmatter.cpp`
- Change `Frontmatter::custom` from `std::map<std::string, std::string>` to `nlohmann::json` (add `#include <nlohmann/json.hpp>`)
- In `frontmatter.cpp`: replace scalar-only block (line 87 `if (!is_known && it->second.IsScalar())`) with a YAML-to-JSON converter that handles scalars, sequences, and maps. Reuse the converter pattern from `data_loader.cpp:82-138`
- In `builder.cpp:485-487`: the `ctx["page"][key] = val` assignment works as-is since `val` is now `nlohmann::json`
- Add tests for array and nested map custom fields

### 3C. Cross-Platform File Watcher
- **Files**: `src/server/dev_server.cpp`, `src/server/dev_server.hpp`
- Replace the single `watch_loop()` (lines 266-329) with three `#ifdef` blocks:

```
#ifdef __APPLE__
  // current kqueue code (unchanged)
#elif defined(__linux__)
  #include <sys/inotify.h>, <poll.h>
  // inotify_init() + inotify_add_watch() per directory
  // poll() with 1s timeout for event loop
  // Same debounce: sleep 150ms, drain remaining
#elif defined(_WIN32)
  #include <windows.h>
  // ReadDirectoryChangesW() per directory
  // WaitForMultipleObjects() in event loop
#else
  // Fallback: polling via std::filesystem::last_write_time
#endif
```

- Move platform `#include`s from file top into `#ifdef` blocks
- Update dev_server.hpp doc comment

### 3D. Windows-Safe Path Handling
- **Files**: `src/utils/path.cpp`, `tests/test_path_utils.cpp`
- Rewrite `path_join`, `source_to_url`, `url_to_output`, `parent_dir`, `replace_extension` to use `std::filesystem::path` operations
- Ensure all URL-returning functions call `.generic_string()` for forward slashes
- Add tests for backslash inputs

### 3E. Integration Tests
- **New file**: `tests/test_build_integration.cpp`
- **Modified**: `tests/CMakeLists.txt`
- Create a fixture that sets up a temp directory with full project structure, calls `build_site()`, and cleans up
- Test cases:
  1. Basic build: verify `output/index.html` exists with correct content
  2. Incremental build: second run has `pages_cached > 0`
  3. Draft exclusion: `pages_skipped == 1`, no draft file in output
  4. Data-driven pages: paginated + per-item pages generated
  5. 404 page: `output/404.html` exists by default
- Add test file to `TEST_SOURCES` in tests/CMakeLists.txt

---

## Phase 4: Performance & Features — DONE

### 4A. --drafts Flag
- **Files**: `src/main.cpp`, `src/pipeline/builder.hpp`, `src/pipeline/builder.cpp`, `tests/test_build_integration.cpp`
- Add `bool include_drafts` flag to both `build` and `serve` subcommands
- Update `build_site()` signature: add `bool include_drafts = false`
- In builder.cpp: change `if (rp.parsed.frontmatter.draft)` to `if (rp.parsed.frontmatter.draft && !include_drafts)`
- When drafts are included, set `ctx["page"]["draft"] = true` so templates can show draft indicators
- Pass `include_drafts` through `cmd_serve` to `build_site()` calls
- Add integration test verifying drafts appear with flag

### 4B. Parallel Page Rendering
- **Depends on**: Phase 3A (Inja caching)
- **Files**: `src/template/renderer.hpp`, `src/template/renderer.cpp`, `src/pipeline/builder.cpp`
- After Phase 1 parsing, pre-load all unique layout templates via `preload_template()` so cache is read-only during rendering
- Replace the sequential rendering loop (builder.cpp:454-505) with parallel version using `std::thread`
- Simple thread pool: `std::vector<std::thread>` with mutex-guarded output queue
- Thread count: `std::min(std::thread::hardware_concurrency(), 4u)` — add `--jobs` / `-j` flag to `build` command
- Pre-allocate `all_outputs` and `all_records` vectors and write to indexed slots to minimize locking
- Note: `inja::Environment::render(template_string, data)` is stateless for the string overload, so safe to call from multiple threads on a shared env

### 4C. JS Regex Literal Handling
- **Files**: `src/assets/asset_pipeline.cpp`, `tests/test_minifier.cpp`
- Rewrite `minify_js()` with a state machine tracking parser context: normal code, string literal, regex literal, comment
- Regex context: after tokens `=`, `(`, `[`, `,`, `;`, `{`, `}`, `!`, `&`, `|`, `^`, `~`, `+`, `-`, `*`, `%`, `<`, `>`, `?`, `:`, `return`, `typeof`, `void`, `delete`, `throw`, `new`, `in`, `case` — a `/` starts a regex
- In regex mode: read until closing `/` (handling `\/` escapes), then read flags (`gimsuy`)
- Pass regex literal through verbatim
- Add tests: `var r = /pattern/g;`, `if (/test/.test(str))`, `var r = /\/path\//;`

---

## Phase 5: Distribution & CI — DONE

### 5A. True Static Binary
- **File**: `CMakeLists.txt`
- Add `option(CSTATIC_STATIC "Build fully static binary" OFF)`
- Linux: when `CSTATIC_STATIC=ON`, add `-static` to linker flags, set `CMAKE_FIND_LIBRARY_SUFFIXES ".a"`
- Windows: set `CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded"` for Release
- macOS: no change (fully static not well-supported)

### 5B. Release Workflow
- **New file**: `.github/workflows/release.yml`
- Triggered on tag push `v*`
- Matrix: macos-latest, ubuntu-latest (with `-DCSTATIC_STATIC=ON`), windows-latest
- Steps: checkout, cmake configure, build, test, upload artifact
- Final job: download all artifacts, create GitHub Release with `gh release create`

---

## Phase 6: Polish — DONE

### 6A. RSS Item Descriptions
- **Files**: `src/pipeline/builder.cpp`, `src/modules/rss.cpp`
- In builder.cpp where `pages_array` is built (~line 434): add `page_meta["excerpt"]` with first 200 chars of rendered HTML, stripped of tags
- Add a `strip_html_tags()` helper to `src/utils/`
- In rss.cpp: add `<description>` element using `page.value("excerpt", "")`

### 6B. Markdown Pagination
- **Files**: `src/config/config.hpp`, `src/config/config.cpp`, `src/pipeline/builder.cpp`, `tests/test_build_integration.cpp`
- Add to config.hpp:
  ```cpp
  struct PaginationRule {
      std::string source;      // "posts" — match pages under src/posts/
      int per_page = 10;
      std::string template_;   // "posts" — template for paginated index
  };
  std::vector<PaginationRule> pagination_rules;
  ```
- Parse `[[pagination]]` TOML array of tables in config.cpp
- In builder.cpp: after markdown pages are collected, apply pagination rules. Group pages by source prefix, split into chunks, render paginated index pages with navigation context (`pagination.page`, `pagination.total_pages`, `pagination.prev_url`, `pagination.next_url`, `pagination.items`)
- Reuse the pattern from `build_paginated_pages()`
- Add integration test

### 6C. Benchmarking Infrastructure
- **New file**: `tests/bench_build.cpp`
- **Modified**: `tests/CMakeLists.txt`
- Use Catch2's `BENCHMARK` macro (available in v3.5.0)
- Benchmarks: `minify_css()` on large CSS, `minify_js()` on large JS, `render_markdown()` on long doc, `parse_frontmatter()` with many fields, `build_site()` on 100+ page site
- Guard with `option(CSTATIC_BUILD_BENCHMARKS "Build benchmarks" OFF)`

---

## File Change Summary

| Phase | New Files | Modified Files |
|---|---|---|
| 1 | `docs/config.md` | `README.md`, `src/main.cpp` |
| 2 | `src/utils/file_io.hpp`, `src/utils/file_io.cpp` | `CMakeLists.txt`, `tests/CMakeLists.txt`, `builder.cpp`, `asset_pipeline.cpp`, `dev_server.cpp`, `sitemap.cpp`, `rss.cpp`, `robots.cpp` |
| 3A | — | `renderer.hpp`, `renderer.cpp` |
| 3B | — | `frontmatter.hpp`, `frontmatter.cpp`, `builder.cpp`, `test_frontmatter.cpp` |
| 3C | — | `dev_server.cpp`, `dev_server.hpp` |
| 3D | — | `path.cpp`, `test_path_utils.cpp` |
| 3E | `tests/test_build_integration.cpp` | `tests/CMakeLists.txt` |
| 4A | — | `main.cpp`, `builder.hpp`, `builder.cpp`, `test_build_integration.cpp` |
| 4B | — | `renderer.hpp`, `renderer.cpp`, `builder.cpp` |
| 4C | — | `asset_pipeline.cpp`, `test_minifier.cpp` |
| 5A | — | `CMakeLists.txt` |
| 5B | `.github/workflows/release.yml` | — |
| 6A | — | `builder.cpp`, `rss.cpp`, (new util helper) |
| 6B | — | `config.hpp`, `config.cpp`, `builder.cpp`, `test_build_integration.cpp` |
| 6C | `tests/bench_build.cpp` | `tests/CMakeLists.txt` |

**Most modified file**: `src/pipeline/builder.cpp` (touched by 6 phases)

---

## Verification

After each phase:
1. `cmake -B build -DCMAKE_BUILD_TYPE=Release -DCSTATIC_BUILD_TESTS=ON && cmake --build build -j$(nproc)`
2. `cd build && ctest --output-on-failure`
3. Manual smoke test: `cstatic init` in a temp dir, `cstatic build`, `cstatic serve` (verify live reload)

After all phases:
4. Verify CI passes on all 3 platforms (macOS, Linux, Windows)
5. Verify `cstatic build --full --drafts` works correctly
6. Verify RSS feed contains `<description>` with content excerpts
7. Verify pagination generates correct page structure
