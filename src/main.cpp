#include <CLI/CLI.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cli/content_generator.hpp"
#include "config/config.hpp"
#include "pipeline/builder.hpp"
#include "server/dev_server.hpp"
#include "utils/terminal.hpp"
#include "utils/path.hpp"

namespace fs = std::filesystem;
using namespace cstatic::utils;

int cmd_init();
int cmd_new(const std::string& path, const std::string& kind);
int cmd_build(bool full_rebuild, bool include_drafts, int jobs, const std::string& env);
int cmd_serve(int port, bool include_drafts, const std::string& env);

int main(int argc, char** argv) {
    CLI::App app{"C-Static — a high-performance static site generator", "cstatic"};

    // --version flag
    app.set_version_flag("--version", CSTATIC_VERSION);

    // init subcommand
    auto* init_cmd = app.add_subcommand("init", "Scaffold a new project");
    init_cmd->callback([]() { std::exit(cmd_init()); });

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
    auto* build_cmd = app.add_subcommand("build", "Build the site");
    build_cmd->add_flag("--full", full_rebuild, "Force a clean rebuild (ignore cache)");
    build_cmd->add_flag("--drafts", include_drafts, "Include draft pages in output");
    build_cmd->add_option("-j,--jobs", jobs, "Number of parallel render threads (0 = auto)")->default_val(0);
    build_cmd->add_option("-e,--env", env, "Build environment (e.g. production)")->default_val("development");
    build_cmd->callback([&full_rebuild, &include_drafts, &jobs, &env]() { std::exit(cmd_build(full_rebuild, include_drafts, jobs, env)); });

    // serve subcommand
    int port = 3000;
    bool serve_include_drafts = false;
    std::string serve_env = "development";
    auto* serve_cmd = app.add_subcommand("serve", "Start dev server with live reload");
    serve_cmd->add_option("--port", port, "Port to serve on")->default_val(3000);
    serve_cmd->add_flag("--drafts", serve_include_drafts, "Include draft pages in output");
    serve_cmd->add_option("-e,--env", serve_env, "Build environment (e.g. production)")->default_val("development");
    serve_cmd->callback([&port, &serve_include_drafts, &serve_env]() { std::exit(cmd_serve(port, serve_include_drafts, serve_env)); });

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

// Format a BuildError for the error summary.
// For template errors with a line number and a resolvable template file,
// includes ±3 lines of source context with a '>' marker.
static std::string format_build_error(const cstatic::BuildError& err,
                                       const std::string& template_dir) {
    std::ostringstream out;

    const char* type_str = "error";
    switch (err.type) {
        case cstatic::BuildError::Type::Template:    type_str = "template"; break;
        case cstatic::BuildError::Type::Frontmatter: type_str = "frontmatter"; break;
        case cstatic::BuildError::Type::Markdown:    type_str = "markdown"; break;
        default: break;
    }

    if (!err.source_file.empty()) {
        out << err.source_file;
    }

    if (!err.template_name.empty()) {
        out << ": " << type_str << " '" << err.template_name << "'";
        if (err.line > 0) {
            std::string tmpl_path = cstatic::utils::path_join(template_dir, err.template_name + ".html");
            out << " (" << tmpl_path << ":" << err.line << ")";
        }
    } else {
        out << ": " << type_str << " error";
    }

    out << "\n    " << err.message;

    // Context lines for template errors with line info
    if (err.line > 0 && !err.template_name.empty()) {
        std::string tmpl_path = cstatic::utils::path_join(template_dir, err.template_name + ".html");
        std::ifstream f(tmpl_path);
        if (f.is_open()) {
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(f, line)) {
                lines.push_back(line);
            }
            int target = err.line;
            int start_line = std::max(1, target - 3);
            int end_line = std::min(static_cast<int>(lines.size()), target + 3);
            for (int l = start_line; l <= end_line; l++) {
                const char* marker = (l == target) ? ">" : " ";
                out << "\n    " << marker << " " << l << " | "
                    << (l <= static_cast<int>(lines.size()) ? lines[l - 1] : "");
            }
        }
    }

    return out.str();
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
    fs::create_directories("src/posts");
    fs::create_directories("templates");
    fs::create_directories("templates/partials");
    fs::create_directories("static/css");
    fs::create_directories("static/js");
    fs::create_directories("shortcodes");
    fs::create_directories("archetypes");

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
html = true

[build.highlight]
enabled = true
style = "github"

[modules]
sitemap = true
rss = false
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

    // Write all files
    struct { const char* path; const char* content; } files[] = {
        {"config.toml",              config_toml},
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

int cmd_build(bool full_rebuild, bool include_drafts, int jobs, const std::string& env) {
    try {
        cstatic::Config cfg = cstatic::load_config("config.toml", env);

        auto result = cstatic::build_site(cfg, full_rebuild, include_drafts, jobs);

        // If errors were collected, print a numbered summary and exit 1
        if (!result.errors.empty()) {
            std::cerr << error_label() << " Build failed with "
                      << result.errors.size() << " error(s):\n\n";
            for (size_t i = 0; i < result.errors.size(); i++) {
                std::cerr << colorize(color::bold, "  " + std::to_string(i + 1) + ". ")
                          << format_build_error(result.errors[i], cfg.template_dir)
                          << "\n\n";
            }
            return 1;
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
