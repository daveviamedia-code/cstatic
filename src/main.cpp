#include <CLI/CLI.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <csignal>
#include <cctype>
#include <ctime>

#include "cli/content_generator.hpp"
#include "cli/error_format.hpp"
#include "config/config.hpp"
#include "pipeline/builder.hpp"
#include "pipeline/link_checker.hpp"
#include "server/dev_server.hpp"
#include "server/file_watcher.hpp"
#include "utils/terminal.hpp"
#include "utils/path.hpp"

namespace fs = std::filesystem;
using namespace cstatic::utils;

int cmd_init(const std::string& name);
int cmd_new(const std::string& path, const std::string& kind);
int cmd_build(bool full_rebuild, bool include_drafts, int jobs, const std::string& env, bool verbose, bool watch);
int cmd_serve(int port, bool include_drafts, const std::string& env);
int cmd_check(bool external_flag, int timeout_ms);

int main(int argc, char** argv) {
    CLI::App app{"C-Static — a high-performance static site generator", "cstatic"};

    // --version flag
    app.set_version_flag("--version", CSTATIC_VERSION);

    // init subcommand
    std::string init_name;
    auto* init_cmd = app.add_subcommand("init", "Scaffold a new project");
    init_cmd->add_option("--name", init_name,
        "Site name (sets config.toml title and the Cloudflare Worker name)");
    init_cmd->callback([&init_name]() { std::exit(cmd_init(init_name)); });

    // new subcommand — create content from an archetype
    std::string new_path, new_kind;
    auto* new_cmd = app.add_subcommand("new", "Create new content from an archetype");
    new_cmd->add_option("path", new_path, "Content path (e.g. posts/my-post.md)")->required();
    new_cmd->add_option("--kind", new_kind, "Archetype name (default: 'default')");
    new_cmd->callback([&new_path, &new_kind]() { std::exit(cmd_new(new_path, new_kind)); });

    // build subcommand
    bool full_rebuild = false;
    bool include_drafts = false;
    int jobs = 0;
    std::string env = "development";
    bool verbose = false;
    bool watch = false;
    auto* build_cmd = app.add_subcommand("build", "Build the site");
    build_cmd->add_flag("--full", full_rebuild, "Force a clean rebuild (ignore cache)");
    build_cmd->add_flag("--drafts", include_drafts, "Include draft pages in output");
    build_cmd->add_option("-j,--jobs", jobs, "Number of parallel render threads (0 = auto)")->default_val(0);
    build_cmd->add_option("-e,--env", env, "Build environment (e.g. production)")->default_val("development");
    build_cmd->add_flag("-v,--verbose", verbose, "Show detailed build diagnostics (phase timing)");
    build_cmd->add_flag("--watch", watch, "Rebuild on file changes (stay running until Ctrl+C)");
    build_cmd->callback([&full_rebuild, &include_drafts, &jobs, &env, &verbose, &watch]() { std::exit(cmd_build(full_rebuild, include_drafts, jobs, env, verbose, watch)); });

    // serve subcommand
    int port = 3000;
    bool serve_include_drafts = false;
    std::string serve_env = "development";
    auto* serve_cmd = app.add_subcommand("serve", "Start dev server with live reload");
    serve_cmd->add_option("--port", port, "Port to serve on")->default_val(3000);
    serve_cmd->add_flag("--drafts", serve_include_drafts, "Include draft pages in output");
    serve_cmd->add_option("-e,--env", serve_env, "Build environment (e.g. production)")->default_val("development");
    serve_cmd->callback([&port, &serve_include_drafts, &serve_env]() { std::exit(cmd_serve(port, serve_include_drafts, serve_env)); });

    // check subcommand
    bool check_external_flag = false;
    int  check_timeout_cli   = 0;
    auto* check_cmd = app.add_subcommand("check", "Check for broken links in built output");
    check_cmd->add_flag("--external", check_external_flag, "Also verify external links via HTTP HEAD");
    check_cmd->add_option("--timeout", check_timeout_cli, "Per-request HTTP timeout in ms (default: config)");
    check_cmd->callback([&check_external_flag, &check_timeout_cli]() { std::exit(cmd_check(check_external_flag, check_timeout_cli)); });

    app.require_subcommand(1);

    CLI11_PARSE(app, argc, argv);
    return 0;
}

// --- Helpers for writing scaffold files ---

static bool write_scaffold_file(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << error_label() << " cannot create file: " << path << "\n";
        return false;
    }
    f << content;
    return true;
}

static void print_created(const std::string& path) {
    std::cout << "  " << colorize(color::green, "created") << "  " << path << "\n";
}

// Build a Cloudflare Worker name from a site title.
// Worker names: lowercase [a-z0-9-], must start with a letter, <= 63 chars.
static std::string worker_name_from_title(const std::string& title) {
    std::string out;
    bool prev_dash = false;
    for (unsigned char c : title) {
        if (std::isalpha(c) || std::isdigit(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
            prev_dash = false;
        } else if (!out.empty() && !prev_dash) {
            out.push_back('-');
            prev_dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.size() > 63) out.resize(63);
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.empty()) return "my-site";
    if (!std::isalpha(static_cast<unsigned char>(out.front()))) out = "site-" + out;
    return out;
}

// Today's date as YYYY-MM-DD (for wrangler compatibility_date).
static std::string current_date_iso() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

// Escape a string for a TOML basic (double-quoted) string.
static std::string toml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

int cmd_init(const std::string& name) {
    // Check if config.toml already exists
    if (fs::exists("config.toml")) {
        std::cerr << error_label() << " config.toml already exists in this directory.\n"
                  << "  Remove it first or choose a different directory.\n";
        return 1;
    }

    std::cout << colorize(color::bold, "Scaffolding new C-Static project...\n\n");

    // Site title: from --name, defaulting to "My Site". Drives config.toml's
    // title and the Cloudflare Worker name in wrangler.jsonc.
    const std::string site_title = name.empty() ? "My Site" : name;

    // Create directories
    fs::create_directories("src");
    fs::create_directories("src/posts");
    fs::create_directories("templates");
    fs::create_directories("templates/partials");
    fs::create_directories("static/css");
    fs::create_directories("static/js");
    fs::create_directories("shortcodes");
    fs::create_directories("archetypes");
    fs::create_directories(".github/workflows");

    // config.toml
    const char* config_toml = R"(# C-Static Site Configuration
# Full docs: https://github.com/daveviamedia-code/cstatic

[site]
title = "%SITE_TITLE%"
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
html = true

[build.highlight]
enabled = true
style = "github"

[modules]
sitemap = true
rss = false
json_feed = false
robots = false

[[collection]]
name = "posts"
template = "post"
index_template = "posts-index"
sort_by = "date"
sort_order = "desc"

[[taxonomy]]
key = "tags"
template = "tag"
index_template = "tags"
)";

    // src/index.md
    const char* index_md = R"(---
title: Home
layout: default
---

# Welcome to C-Static

Your new static site is ready. Edit this file in `src/index.md` to get started.

## Quick Start

1. Edit pages in `src/`
2. Customize templates in `templates/`
3. Run `cstatic build` to generate your site
4. Run `cstatic serve` to preview with live reload
)";

    // src/about.md
    const char* about_md = R"(---
title: About
layout: default
date: "2025-01-01"
---

# About

This is the about page. Replace this content with your own.
)";

    // templates/default.html
    const char* default_html = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{{ page.title }} — {{ site.title }}</title>
  {{ seo_meta }}
  <link rel="stylesheet" href="/css/style.css">
  <link rel="stylesheet" href="/css/highlight.css">
</head>
<body>
  <nav>
    <a href="/">{{ site.title }}</a>
  </nav>
  <main>
    {{ page.content }}
  </main>
  <footer>
    <p>Built with <a href="https://github.com/daveviamedia-code/cstatic">C-Static</a></p>
  </footer>
</body>
</html>
)";

    // static/css/style.css
    const char* style_css = R"(/* Minimal reset */
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

body {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  line-height: 1.6;
  color: #333;
  max-width: 48rem;
  margin: 0 auto;
  padding: 2rem 1rem;
}

nav { margin-bottom: 2rem; }
nav a { font-weight: bold; text-decoration: none; color: #0066cc; }

main h1 { margin-bottom: 1rem; }
main h2 { margin-top: 1.5rem; margin-bottom: 0.5rem; }
main p { margin-bottom: 0.75rem; }
main ul, main ol { margin-bottom: 0.75rem; padding-left: 1.5rem; }
main a { color: #0066cc; }

footer { margin-top: 3rem; padding-top: 1rem; border-top: 1px solid #eee; color: #999; font-size: 0.875rem; }
)";

    // static/js/app.js
    const char* app_js = "// Add your JavaScript here.\n";

    // templates/partials/nav.html
    const char* nav_html = R"(<nav>
  <a href="/">{{ site.title }}</a>
  {% for p in pages %}
  <a href="{{ p.url }}">{{ p.title }}</a>
  {% endfor %}
</nav>
)";

    // src/posts/first-post.md
    const char* first_post_md = R"(---
title: My First Post
date: "2025-01-15"
tags: [getting-started]
---

# My First Post

Welcome to your blog! This is your first post. Edit or delete it, then run `cstatic build` to see the result.
)";

    // templates/post.html
    const char* post_html = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{{ page.title }} — {{ site.title }}</title>
  {{ seo_meta }}
  <link rel="stylesheet" href="/css/style.css">
  <link rel="stylesheet" href="/css/highlight.css">
</head>
<body>
  <nav>
    <a href="/">{{ site.title }}</a>
  </nav>
  <article>
    {{ page.content }}
  </article>
  <footer>
    <p>Built with <a href="https://github.com/daveviamedia-code/cstatic">C-Static</a></p>
  </footer>
</body>
</html>
)";

    // templates/posts-index.html
    const char* posts_index_html = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Posts — {{ site.title }}</title>
  <link rel="stylesheet" href="/css/style.css">
</head>
<body>
  <nav>
    <a href="/">{{ site.title }}</a>
  </nav>
  <main>
    <h1>Posts</h1>
    <ul>
    {% for p in collection.pages %}
      <li><a href="{{ p.url }}">{{ p.title }}</a></li>
    {% endfor %}
    </ul>
  </main>
  <footer>
    <p>Built with <a href="https://github.com/daveviamedia-code/cstatic">C-Static</a></p>
  </footer>
</body>
</html>
)";

    // templates/tag.html
    const char* tag_html = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Tag: {{ taxonomy.term }} — {{ site.title }}</title>
  <link rel="stylesheet" href="/css/style.css">
</head>
<body>
  <nav>
    <a href="/">{{ site.title }}</a>
  </nav>
  <main>
    <h1>Tagged: {{ taxonomy.term }}</h1>
    <ul>
    {% for p in taxonomy.pages %}
      <li><a href="{{ p.url }}">{{ p.title }}</a></li>
    {% endfor %}
    </ul>
    <p><a href="/tags/">All tags</a></p>
  </main>
  <footer>
    <p>Built with <a href="https://github.com/daveviamedia-code/cstatic">C-Static</a></p>
  </footer>
</body>
</html>
)";

    // templates/tags.html
    const char* tags_html = R"(<!DOCTYPE html>
<html lang="{{ site.language }}">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Tags — {{ site.title }}</title>
  <link rel="stylesheet" href="/css/style.css">
</head>
<body>
  <nav>
    <a href="/">{{ site.title }}</a>
  </nav>
  <main>
    <h1>Tags</h1>
    <ul>
    {% for t in taxonomy.terms %}
      <li><a href="{{ t.url }}">{{ t.term }}</a> ({{ t.count }})</li>
    {% endfor %}
    </ul>
  </main>
  <footer>
    <p>Built with <a href="https://github.com/daveviamedia-code/cstatic">C-Static</a></p>
  </footer>
</body>
</html>
)";

    // templates/og-default.svg — Open Graph image template (1200x630).
    // Rendered per-page with {{ page.title }}, {{ page.date }}, {{ site.title }}.
    // Enable via [og_images] enabled = true in config.toml.
    const char* og_default_svg = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="630" viewBox="0 0 1200 630">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#0f172a"/>
      <stop offset="1" stop-color="#1e293b"/>
    </linearGradient>
  </defs>
  <rect width="1200" height="630" fill="url(#bg)"/>
  <rect x="0" y="0" width="1200" height="10" fill="#3b82f6"/>
  <text x="80" y="300" font-family="Georgia, 'Times New Roman', serif" font-size="68" font-weight="bold" fill="#f8fafc">{{ page.title }}</text>
  <text x="80" y="380" font-family="-apple-system, 'Segoe UI', Roboto, sans-serif" font-size="30" fill="#64748b">{{ page.date }}</text>
  <text x="80" y="560" font-family="-apple-system, 'Segoe UI', Roboto, sans-serif" font-size="32" fill="#94a3b8">{{ site.title }}</text>
</svg>
)SVG";

    // shortcodes/youtube.html — embed a YouTube video by ID.
    // Usage in markdown: {{< youtube dQw4w9WgXcQ >}}
    const char* shortcode_youtube_html = R"(<div class="video-embed">
  <iframe width="560" height="315"
          src="https://www.youtube.com/embed/{{ params.0 }}"
          title="YouTube video player"
          frameborder="0"
          allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture"
          allowfullscreen></iframe>
</div>
)";

    // shortcodes/figure.html — captioned figure with named params.
    // Usage: {{< figure src="/images/cat.jpg" alt="A cat" caption="Mittens" >}}
    const char* shortcode_figure_html = R"(<figure>
  <img src="{{ named.src }}" alt="{{ named.alt }}" {% if named.caption %}aria-describedby="fig-caption"{% endif %}>
  {% if named.caption %}<figcaption id="fig-caption">{{ named.caption }}</figcaption>{% endif %}
</figure>
)";

    // shortcodes/note.html — block shortcode example (callout/admonition).
    // Usage: {{< note >}}This is important.{{< /note >}}
    const char* shortcode_note_html = R"(<aside class="note">
  <strong>Note:</strong> {{ content }}
</aside>
)";

    // archetypes/default.md — fallback template for `cstatic new`.
    // Placeholders {{ title }}, {{ date }}, {{ slug }} are substituted.
    const char* archetype_default_md = R"(---
title: "{{ title }}"
date: "{{ date }}"
---

# {{ title }}
)";

    // archetypes/post.md — used with `cstatic new --kind post posts/x.md`.
    // New posts start as drafts so they don't ship until ready.
    const char* archetype_post_md = R"(---
title: "{{ title }}"
date: "{{ date }}"
tags: []
draft: true
---

# {{ title }}
)";

    // .github/workflows/deploy.yml — push-to-deploy to Cloudflare Workers.
    // Downloads the cstatic release binary (no C++ toolchain needed in CI),
    // builds the site, and uploads output/ as a Worker's static assets.
    const char* deploy_yml = R"YAML(# Deploys the built site to Cloudflare Workers on every push to main.
# Requires two repository secrets (Settings -> Secrets and variables -> Actions):
#   CLOUDFLARE_API_TOKEN  - token with "Workers Scripts: Edit" + "Account: Read"
#   CLOUDFLARE_ACCOUNT_ID - your Cloudflare account ID
# First run creates the Worker; later runs update it.

name: Deploy

on:
  push:
    branches: [main]
  workflow_dispatch: {}

env:
  # GitHub repo that publishes the cstatic release binary. Change if you fork.
  CSTATIC_REPO: daveviamedia-code/cstatic

jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Download cstatic
        run: |
          curl -fsSL "https://github.com/${CSTATIC_REPO}/releases/latest/download/cstatic-linux-x86_64" -o cstatic
          chmod +x cstatic

      - name: Build site
        run: ./cstatic build --env production

      - name: Deploy to Cloudflare Workers
        uses: cloudflare/wrangler-action@v3
        with:
          apiToken: ${{ secrets.CLOUDFLARE_API_TOKEN }}
          accountId: ${{ secrets.CLOUDFLARE_ACCOUNT_ID }}
          command: deploy
)YAML";

    // .gitignore — keep build output and tooling caches out of the repo.
    const char* gitignore = R"GIT(# C-Static build output & cache
output/
.cstatic_cache/

# Wrangler / Node (only relevant if you install wrangler locally)
.wrangler/
node_modules/

# OS
.DS_Store
Thumbs.db
)GIT";

    // config.toml title is templated from --name (escaped for TOML).
    std::string config_content = config_toml;
    {
        const std::string marker = "%SITE_TITLE%";
        size_t pos = config_content.find(marker);
        if (pos != std::string::npos) {
            config_content.replace(pos, marker.size(), toml_escape(site_title));
        }
    }

    // Write all files
    struct { const char* path; std::string content; } files[] = {
        {"config.toml",              config_content},
        {"src/index.md",             index_md},
        {"src/about.md",             about_md},
        {"src/posts/first-post.md",  first_post_md},
        {"templates/default.html",   default_html},
        {"templates/post.html",      post_html},
        {"templates/posts-index.html", posts_index_html},
        {"templates/tag.html",       tag_html},
        {"templates/tags.html",      tags_html},
        {"templates/partials/nav.html", nav_html},
        {"templates/og-default.svg", og_default_svg},
        {"shortcodes/youtube.html",  shortcode_youtube_html},
        {"shortcodes/figure.html",   shortcode_figure_html},
        {"shortcodes/note.html",     shortcode_note_html},
        {"archetypes/default.md",    archetype_default_md},
        {"archetypes/post.md",       archetype_post_md},
        {"static/css/style.css",     style_css},
        {"static/js/app.js",         app_js},
        {".github/workflows/deploy.yml", deploy_yml},
        {".gitignore",               gitignore},
    };

    for (const auto& f : files) {
        if (!write_scaffold_file(f.path, f.content)) {
            return 1;
        }
        print_created(f.path);
    }

    // wrangler.jsonc — Cloudflare Workers config (assets-only, no Worker script).
    // Templated: worker name slugified from the site title, compatibility date
    // set to today.
    {
        std::string worker_name = worker_name_from_title(site_title);
        std::string today = current_date_iso();
        std::string wrangler_jsonc =
            "{\n"
            "  // Cloudflare Worker name (lowercase). Edit freely; must be unique\n"
            "  // per account. No \"main\" field => Cloudflare serves ./output as\n"
            "  // static files directly (no Worker code runs).\n"
            "  \"name\": \"" + worker_name + "\",\n"
            "  // Date you first developed against Cloudflare's runtime.\n"
            "  \"compatibility_date\": \"" + today + "\",\n"
            "  \"assets\": {\n"
            "    \"directory\": \"./output\",\n"
            "    // C-Static emits output/404.html; serve it for unmatched paths.\n"
            "    \"not_found_handling\": \"404-page\"\n"
            "  }\n"
            "}\n";
        if (!write_scaffold_file("wrangler.jsonc", wrangler_jsonc)) return 1;
        print_created("wrangler.jsonc");
    }

    std::cout << "\n" << success_label() << " Project scaffolded.\n\n"
              << colorize(color::bold, "Next steps:\n")
              << "  1. Edit " << colorize(color::cyan, "src/index.md") << " to customize your home page\n"
              << "  2. Run " << colorize(color::cyan, "cstatic serve") << " to preview at "
              << colorize(color::cyan, "http://localhost:3000") << "\n"
              << "  3. Run " << colorize(color::cyan, "cstatic build") << " to generate the output\n"
              << "  4. Push to " << colorize(color::cyan, "GitHub")
              << " to deploy to Cloudflare Workers (set "
              << colorize(color::cyan, "CLOUDFLARE_API_TOKEN") << " + "
              << colorize(color::cyan, "CLOUDFLARE_ACCOUNT_ID")
              << " secrets; see " << colorize(color::cyan, ".github/workflows/deploy.yml") << ")\n";

    return 0;
}

int cmd_new(const std::string& path, const std::string& kind) {
    cstatic::Config cfg;
    try {
        cfg = cstatic::load_config("config.toml");
    } catch (const cstatic::ConfigError& e) {
        std::cerr << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << error_label() << " " << e.what() << "\n";
        return 1;
    }
    std::string target = path_join(cfg.source_dir, path);
    return cstatic::cli::generate_content(target, "archetypes", kind);
}

int cmd_build(bool full_rebuild, bool include_drafts, int jobs, const std::string& env, bool verbose, bool watch) {
    try {
        cstatic::Config cfg = cstatic::load_config("config.toml", env);

        auto result = cstatic::build_site(cfg, full_rebuild, include_drafts, jobs);

        // If errors were collected, print a numbered summary and exit 1
        if (!result.errors.empty()) {
            std::cerr << error_label() << " Build failed with "
                      << result.errors.size() << " error(s):\n\n";
            for (size_t i = 0; i < result.errors.size(); i++) {
                std::cerr << colorize(color::bold, "  " + std::to_string(i + 1) + ". ")
                          << cstatic::cli::format_build_error(result.errors[i], cfg.template_dir)
                          << "\n\n";
            }
            return 1;
        }

        // Verbose: per-phase timing breakdown.
        if (verbose) {
            std::cerr << info_label() << " phase timing:\n"
                      << "    parse+render md : " << static_cast<int>(result.phase1_ms) << " ms\n"
                      << "    render templates: " << static_cast<int>(result.phase2_ms) << " ms\n"
                      << "    data+alias pages: " << static_cast<int>(result.phase3_ms) << " ms\n"
                      << "    asset pipeline  : " << static_cast<int>(result.asset_ms) << " ms\n"
                      << "    modules         : " << static_cast<int>(result.module_ms) << " ms\n";
        }

        // Build stats in green
        if (result.pages_cached > 0) {
            std::cout << colorize(color::dim, "Cache hit: " + std::to_string(result.pages_cached) + " page(s) unchanged\n");
            std::cout << colorize(color::green, "Rebuilt " + std::to_string(result.pages_built) + " page(s)");
        } else {
            std::cout << colorize(color::green, "Built " + std::to_string(result.pages_built) + " page(s)");
        }

        if (result.pages_skipped > 0) {
            std::cout << colorize(color::yellow, " (" + std::to_string(result.pages_skipped) + " draft(s) skipped)");
        }
        if (result.pages_scheduled > 0) {
            std::cout << colorize(color::dim, " (" + std::to_string(result.pages_scheduled) + " scheduled)");
        }
        if (result.pages_removed > 0) {
            std::cout << colorize(color::yellow, " (" + std::to_string(result.pages_removed) + " orphan(s) removed)");
        }
        std::cout << " in " << static_cast<int>(result.elapsed_ms) << "ms\n";

        if (result.assets_copied > 0 || result.assets_minified > 0 || result.assets_cached > 0) {
            bool first = true;
            std::cout << colorize(color::dim, "Assets: ");
            if (result.assets_minified > 0) {
                std::cout << result.assets_minified << " minified";
                if (result.bytes_saved > 0) {
                    std::cout << " (" << result.bytes_saved << " bytes saved)";
                }
                first = false;
            }
            if (result.assets_copied > 0) {
                if (!first) std::cout << ", ";
                std::cout << result.assets_copied << " copied";
                first = false;
            }
            if (result.assets_cached > 0) {
                if (!first) std::cout << ", ";
                std::cout << result.assets_cached << " cached";
                first = false;
            }
            if (result.assets_removed > 0) {
                if (!first) std::cout << ", ";
                std::cout << result.assets_removed << " removed";
            }
            std::cout << "\n";
        }

        if (watch) {
            // --- Watch mode: stay running, rebuild on file changes ---
            std::cout << "\n  " << colorize(color::bold,
                              colorize(color::green, "Watching for changes"))
                      << "  " << colorize(color::dim, "(Ctrl+C to stop)\n\n") << std::flush;

            // Same watch set the dev server uses: source, templates, static,
            // and the project root (so config.toml / archetypes trigger too).
            std::vector<std::string> watch_dirs;
            for (const auto& dir : {cfg.source_dir, cfg.template_dir, cfg.static_dir, std::string(".")}) {
                if (fs::exists(dir)) watch_dirs.push_back(dir);
            }

            // Rebuild callback — mirrors DevServer::rebuild_and_reload() minus
            // the SSE broadcast. Reloads config each time so config.toml edits
            // take effect without restart.
            auto rebuild = [include_drafts]() {
                try {
                    cstatic::Config current = cstatic::load_config("config.toml");
                    auto r = cstatic::build_site(current, false, include_drafts);

                    // Stale-cache self-heal: retry as full rebuild if the
                    // incremental pass reported no work and no errors.
                    const bool suspicious_zero = current.incremental_enabled &&
                        r.pages_built == 0 &&
                        r.pages_cached == 0 &&
                        r.errors.empty();
                    if (suspicious_zero) {
                        std::cerr << "  " << warning_label()
                                  << " incremental rebuild reported no work; retrying as full rebuild\n";
                        r = cstatic::build_site(current, true, include_drafts);
                    }

                    std::string msg;
                    if (r.pages_cached > 0) {
                        msg = "[" + std::to_string(static_cast<int>(r.elapsed_ms)) + "ms] " +
                              std::to_string(r.pages_built) + " rebuilt, " +
                              std::to_string(r.pages_cached) + " cached";
                    } else {
                        msg = "[" + std::to_string(static_cast<int>(r.elapsed_ms)) + "ms] " +
                              std::to_string(r.pages_built) + " page(s) built";
                    }
                    std::cout << "  " << colorize(color::dim, msg) << "\n" << std::flush;
                } catch (const std::exception& e) {
                    std::cerr << "  " << error_label() << " rebuild failed: "
                              << e.what() << "\n";
                }
            };

            cstatic::FileWatcher file_watcher(std::move(watch_dirs), rebuild);

            // SIGINT → stop the watcher. Function-local static so the C signal
            // handler can reach it; safe to call from any thread.
            static cstatic::FileWatcher* watcher_ptr = &file_watcher;
            std::signal(SIGINT, [](int) {
                if (watcher_ptr) watcher_ptr->stop();
            });

            // Blocks until SIGINT (or no dirs could be opened).
            file_watcher.start();

            std::cout << "\n  Stopped.\n";
            return 0;
        }

        std::cout << success_label() << " Build complete.\n";
        return 0;
    } catch (const cstatic::ConfigError& e) {
        std::cerr << e.what() << "\n";
        return 1;
    } catch (const std::runtime_error& e) {
        std::cerr << error_label() << " " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << error_label() << " unexpected error: " << e.what() << "\n";
        return 1;
    }
}

int cmd_serve(int port, bool include_drafts, const std::string& env) {
    try {
        cstatic::Config cfg = cstatic::load_config("config.toml", env);

        // Run an initial build before serving
        auto result = cstatic::build_site(cfg, false, include_drafts);
        std::cout << colorize(color::green, "Built " + std::to_string(result.pages_built) + " page(s)")
                  << " in " << static_cast<int>(result.elapsed_ms) << "ms\n\n";

        std::cout << "  " << colorize(color::cyan, "Local:   http://localhost:" + std::to_string(port)) << "\n\n";

        cstatic::DevServer server(cfg, port, include_drafts);
        server.start();
        return 0;
    } catch (const cstatic::ConfigError& e) {
        std::cerr << e.what() << "\n";
        return 1;
    } catch (const std::runtime_error& e) {
        std::cerr << error_label() << " " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << error_label() << " unexpected error: " << e.what() << "\n";
        return 1;
    }
}

int cmd_check(bool external_flag, int timeout_ms_cli) {
    try {
        cstatic::Config cfg = cstatic::load_config("config.toml");

        bool do_external = external_flag || cfg.check_external;
        int  timeout_ms  = (timeout_ms_cli > 0) ? timeout_ms_cli : cfg.check_timeout_ms;

        cstatic::pipeline::CheckResult r =
            cstatic::pipeline::check_links(cfg.output_dir, do_external, timeout_ms);

        // Per-issue lines. External transport failures were already surfaced
        // as warnings inside check_links; here we only print counted issues.
        for (const auto& issue : r.issues) {
            std::cerr << error_label() << " " << issue.source_file;
            if (issue.line > 0) std::cerr << ":" << issue.line;
            std::cerr << "\n    " << colorize(color::bold, issue.href)
                      << "  " << colorize(color::dim, issue.message) << "\n";
        }

        // Summary line.
        std::cout << colorize(color::dim,
            "Checked " + std::to_string(r.internal_checked) + " internal link(s)");
        if (do_external) {
            std::cout << colorize(color::dim,
                ", " + std::to_string(r.external_checked) + " external URL(s)");
        }
        std::cout << "\n";

        if (r.issues.empty()) {
            std::cout << success_label() << " No broken links found.\n";
            return 0;
        }
        std::cout << error_label() << " Found " << r.issues.size() << " broken link(s).\n";
        return 1;
    } catch (const cstatic::ConfigError& e) {
        std::cerr << e.what() << "\n";
        return 1;
    } catch (const std::runtime_error& e) {
        std::cerr << error_label() << " " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << error_label() << " unexpected error: " << e.what() << "\n";
        return 1;
    }
}
