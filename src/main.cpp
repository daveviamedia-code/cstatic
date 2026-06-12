#include <CLI/CLI.hpp>
#include <iostream>
#include <string>

#include "config/config.hpp"
#include "pipeline/builder.hpp"
#include "server/dev_server.hpp"
#include "utils/terminal.hpp"

namespace fs = std::filesystem;
using namespace cstatic::utils;

int cmd_init();
int cmd_build(bool full_rebuild, bool include_drafts, int jobs);
int cmd_serve(int port, bool include_drafts);

int main(int argc, char** argv) {
    CLI::App app{"C-Static — a high-performance static site generator", "cstatic"};

    // --version flag
    app.set_version_flag("--version", CSTATIC_VERSION);

    // init subcommand
    auto* init_cmd = app.add_subcommand("init", "Scaffold a new project");
    init_cmd->callback([]() { std::exit(cmd_init()); });

    // build subcommand
    bool full_rebuild = false;
    bool include_drafts = false;
    int jobs = 0;
    auto* build_cmd = app.add_subcommand("build", "Build the site");
    build_cmd->add_flag("--full", full_rebuild, "Force a clean rebuild (ignore cache)");
    build_cmd->add_flag("--drafts", include_drafts, "Include draft pages in output");
    build_cmd->add_option("-j,--jobs", jobs, "Number of parallel render threads (0 = auto)")->default_val(0);
    build_cmd->callback([&full_rebuild, &include_drafts, &jobs]() { std::exit(cmd_build(full_rebuild, include_drafts, jobs)); });

    // serve subcommand
    int port = 3000;
    bool serve_include_drafts = false;
    auto* serve_cmd = app.add_subcommand("serve", "Start dev server with live reload");
    serve_cmd->add_option("--port", port, "Port to serve on")->default_val(3000);
    serve_cmd->add_flag("--drafts", serve_include_drafts, "Include draft pages in output");
    serve_cmd->callback([&port, &serve_include_drafts]() { std::exit(cmd_serve(port, serve_include_drafts)); });

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

int cmd_init() {
    // Check if config.toml already exists
    if (fs::exists("config.toml")) {
        std::cerr << error_label() << " config.toml already exists in this directory.\n"
                  << "  Remove it first or choose a different directory.\n";
        return 1;
    }

    std::cout << colorize(color::bold, "Scaffolding new C-Static project...\n\n");

    // Create directories
    fs::create_directories("src");
    fs::create_directories("templates");
    fs::create_directories("static/css");
    fs::create_directories("static/js");

    // config.toml
    const char* config_toml = R"(# C-Static Site Configuration
# Full docs: https://github.com/daveviamedia-code/cstatic

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
  <link rel="stylesheet" href="/css/style.css">
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

    // Write all files
    struct { const char* path; const char* content; } files[] = {
        {"config.toml",              config_toml},
        {"src/index.md",             index_md},
        {"src/about.md",             about_md},
        {"templates/default.html",   default_html},
        {"static/css/style.css",     style_css},
        {"static/js/app.js",         app_js},
    };

    for (const auto& f : files) {
        if (!write_scaffold_file(f.path, f.content)) {
            return 1;
        }
        print_created(f.path);
    }

    std::cout << "\n" << success_label() << " Project scaffolded.\n\n"
              << colorize(color::bold, "Next steps:\n")
              << "  1. Edit " << colorize(color::cyan, "src/index.md") << " to customize your home page\n"
              << "  2. Run " << colorize(color::cyan, "cstatic serve") << " to preview at "
              << colorize(color::cyan, "http://localhost:3000") << "\n"
              << "  3. Run " << colorize(color::cyan, "cstatic build") << " to generate the output\n";

    return 0;
}

int cmd_build(bool full_rebuild, bool include_drafts, int jobs) {
    try {
        cstatic::Config cfg = cstatic::load_config("config.toml");

        auto result = cstatic::build_site(cfg, full_rebuild, include_drafts, jobs);

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

int cmd_serve(int port, bool include_drafts) {
    try {
        cstatic::Config cfg = cstatic::load_config("config.toml");

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
