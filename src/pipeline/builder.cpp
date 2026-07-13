#include "pipeline/builder.hpp"
#include "assets/asset_pipeline.hpp"
#include "config/config.hpp"
#include "content/authors_index.hpp"
#include "content/frontmatter.hpp"
#include "content/link_graph.hpp"
#include "content/markdown.hpp"
#include "content/passage_index.hpp"
#include "content/readability.hpp"
#include "content/toc.hpp"
#include "content/schema_blocks.hpp"
#include "content/shortcodes.hpp"
#include "data/data_loader.hpp"
#include "hash/hash_store.hpp"
#include "modules/sitemap.hpp"
#include "modules/rss.hpp"
#include "modules/json_feed.hpp"
#include "modules/llms_txt.hpp"
#include "modules/robots.hpp"
#include "modules/seo_schema.hpp"
#include "modules/search.hpp"
#include "modules/og_images.hpp"
#include "template/renderer.hpp"
#include "utils/path.hpp"
#include "utils/terminal.hpp"
#include "utils/file_io.hpp"

#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <mutex>
#include <regex>
#include <thread>
#include <unordered_set>
#include <unordered_map>
#include <map>

namespace cstatic {

namespace fs = std::filesystem;

// Collect all files with a given extension under a directory recursively.
static std::vector<std::string> collect_files(const std::string& dir, const std::string& ext) {
    std::vector<std::string> files;
    if (!fs::exists(dir)) return files;

    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string path = entry.path().string();
        if (path.size() > ext.size() && path.substr(path.size() - ext.size()) == ext) {
            files.push_back(path);
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Collect all data files (JSON + YAML) from the data directory.
static std::vector<std::string> collect_data_files(const std::string& data_dir) {
    auto json_files = collect_files(data_dir, ".json");
    auto yaml_files = collect_files(data_dir, ".yaml");
    auto yml_files  = collect_files(data_dir, ".yml");

    json_files.insert(json_files.end(), yaml_files.begin(), yaml_files.end());
    json_files.insert(json_files.end(), yml_files.begin(), yml_files.end());
    return json_files;
}

// Collect all template files from the template directory.
static std::vector<std::string> collect_template_files(const std::string& template_dir) {
    return collect_files(template_dir, ".html");
}

// --- URL interpolation for data-driven pages ---

static std::string interpolate_url(const std::string& pattern, const nlohmann::json& item) {
    static const std::regex tag_pattern(R"(\{\{\s*(\w+)\s*\}\})");
    std::string result = pattern;
    std::smatch match;
    std::string remaining = result;
    std::string built;

    while (std::regex_search(remaining, match, tag_pattern)) {
        built += match.prefix().str();
        std::string key = match[1].str();
        if (item.contains(key)) {
            if (item[key].is_string()) {
                built += item[key].get<std::string>();
            } else {
                built += item[key].dump();
            }
        } else {
            built += "{{ " + key + " }}";
        }
        remaining = match.suffix().str();
    }
    built += remaining;
    return built;
}

// --- Cached output store ---
// Stores rendered HTML keyed by output path for incremental reuse.

struct CachedOutput {
    std::string output_path;
    std::string html;
};

// --- Page record for dependency tracking ---

struct PageRecord {
    std::string output_path;             // e.g. "output/posts/hello/index.html"
    std::string url;                     // e.g. "/posts/hello/"
    std::vector<std::string> deps;       // hash keys this page depends on
};

// Build an HTML string of SEO meta tags (Open Graph, Twitter Card, canonical link).
// Injected into templates as {{ seo_meta }}.
static std::string build_seo_meta(
    const std::string& title, const std::string& url, const std::string& description,
    const std::string& excerpt, const std::string& image, const std::string& canonical,
    const std::string& base_url, const std::string& twitter_handle,
    const std::string& tldr = "")
{
    // G9: tldr is the most concise description and wins over everything.
    std::string desc;
    if (!tldr.empty()) desc = tldr;
    else desc = !description.empty() ? description : excerpt;
    std::string card = !image.empty() ? "summary_large_image" : "summary";
    std::string canonical_url = !canonical.empty() ? canonical : (base_url + url);

    std::string image_url = image;
    if (!image_url.empty() && image_url.front() == '/') {
        image_url = base_url + image_url;
    }

    std::ostringstream out;
    if (!desc.empty()) {
        out << "<meta name=\"description\" content=\""
            << utils::xml_escape(desc) << "\">\n";
    }
    out << "<meta property=\"og:title\" content=\""
        << utils::xml_escape(title) << "\">\n";
    if (!desc.empty()) {
        out << "<meta property=\"og:description\" content=\""
            << utils::xml_escape(desc) << "\">\n";
    }
    out << "<meta property=\"og:url\" content=\""
        << utils::xml_escape(canonical_url) << "\">\n";
    if (!image_url.empty()) {
        out << "<meta property=\"og:image\" content=\""
            << utils::xml_escape(image_url) << "\">\n";
    }
    out << "<meta name=\"twitter:card\" content=\"" << card << "\">\n";
    if (!twitter_handle.empty()) {
        out << "<meta name=\"twitter:site\" content=\""
            << utils::xml_escape(twitter_handle) << "\">\n";
    }
    out << "<link rel=\"canonical\" href=\""
        << utils::xml_escape(canonical_url) << "\">\n";

    return out.str();
}

// --- Data-driven page builders ---

static void build_paginated_pages(
    const Config::DataSource& ds,
    const nlohmann::json& items,
    const nlohmann::json& site_ctx,
    const nlohmann::json& pages_array,
    const TemplateRenderer& renderer,
    const std::string& output_dir,
    const std::string& data_file_key,
    const std::string& template_key,
    std::vector<CachedOutput>& outputs,
    std::vector<PageRecord>& records,
    int& pages_built,
    std::vector<BuildError>* errors = nullptr
) {
    if (!items.is_array() || items.empty()) return;

    int total_items = static_cast<int>(items.size());
    int per_page = ds.per_page > 0 ? ds.per_page : total_items;
    int total_pages = (total_items + per_page - 1) / per_page;

    std::string base_url;
    if (!ds.url_pattern.empty()) {
        static const std::regex base_pattern(R"(([^{]*)\{\{)");
        std::smatch match;
        if (std::regex_search(ds.url_pattern, match, base_pattern)) {
            base_url = match[1].str();
        } else {
            base_url = ds.url_pattern;
        }
    } else {
        base_url = "/" + ds.template_name + "s/";
    }
    if (!base_url.empty() && base_url.back() != '/') base_url += '/';

    for (int page = 0; page < total_pages; page++) {
        int start = page * per_page;
        int end = std::min(start + per_page, total_items);

        nlohmann::json page_items = nlohmann::json::array();
        for (int i = start; i < end; i++) {
            page_items.push_back(items[i]);
        }

        nlohmann::json pagination;
        pagination["page"] = page + 1;
        pagination["total_pages"] = total_pages;
        pagination["total_items"] = total_items;
        pagination["per_page"] = per_page;
        pagination["items"] = page_items;
        pagination["prev_url"] = "";
        pagination["next_url"] = "";

        if (page > 0) {
            pagination["prev_url"] = (page == 1) ? base_url
                : base_url + "page/" + std::to_string(page) + "/";
        }
        if (page < total_pages - 1) {
            pagination["next_url"] = base_url + "page/" + std::to_string(page + 2) + "/";
        }

        std::string page_url = (page == 0) ? base_url
            : base_url + "page/" + std::to_string(page + 1) + "/";
        std::string output_path = utils::url_to_output(page_url, output_dir);

        nlohmann::json ctx;
        ctx["site"] = site_ctx;
        ctx["pages"] = pages_array;
        ctx["pagination"] = pagination;
        ctx["page"] = nlohmann::json::object();
        ctx["page"]["url"] = page_url;
        ctx["page"]["title"] = ds.template_name;
        ctx["page"]["content"] = "";

        std::string html;
        try {
            html = renderer.render(ds.template_name, ctx, ds.file);
        } catch (const RenderError& e) {
            if (errors) {
                errors->push_back({BuildError::Type::Template, e.source_file(),
                                   e.template_name(), e.line(), 0, e.what()});
            }
            continue;
        } catch (const std::runtime_error& e) {
            if (errors) {
                errors->push_back({BuildError::Type::Generic, ds.file,
                                   ds.template_name, 0, 0, e.what()});
            }
            continue;
        }
        outputs.push_back({output_path, html});

        PageRecord rec;
        rec.output_path = output_path;
        rec.url = page_url;
        rec.deps = {data_file_key, template_key};
        records.push_back(std::move(rec));

        pages_built++;
    }
}

static void build_per_item_pages(
    const Config& cfg,
    const Config::DataSource& ds,
    const nlohmann::json& items,
    const nlohmann::json& site_ctx,
    const nlohmann::json& pages_array,
    const TemplateRenderer& renderer,
    const std::string& output_dir,
    const std::string& data_file_key,
    const std::string& template_key,
    std::vector<CachedOutput>& outputs,
    std::vector<PageRecord>& records,
    int& pages_built,
    std::vector<BuildError>* errors = nullptr
) {
    if (!items.is_array()) return;

    for (const auto& item : items) {
        std::string item_url;
        if (!ds.url_pattern.empty()) {
            item_url = interpolate_url(ds.url_pattern, item);
        } else {
            std::string slug;
            if (item.contains(ds.item_key) && item[ds.item_key].is_string()) {
                slug = item[ds.item_key].get<std::string>();
            } else if (item.contains("slug") && item["slug"].is_string()) {
                slug = item["slug"].get<std::string>();
            } else if (item.contains("title") && item["title"].is_string()) {
                slug = item["title"].get<std::string>();
                std::transform(slug.begin(), slug.end(), slug.begin(),
                    [](unsigned char c) { return c == ' ' ? '-' : std::tolower(c); });
            } else {
                continue;
            }
            item_url = "/" + ds.template_name + "s/" + slug + "/";
        }

        if (!item_url.empty() && item_url.back() != '/') item_url += '/';
        if (!item_url.empty() && item_url.front() != '/') item_url = '/' + item_url;

        std::string output_path = utils::url_to_output(item_url, output_dir);

        nlohmann::json ctx;
        ctx["site"] = site_ctx;
        ctx["pages"] = pages_array;
        ctx["item"] = item;
        ctx["page"] = item;
        ctx["page"]["url"] = item_url;

        ctx["seo_meta"] = build_seo_meta(
            item.value("title", ""), item_url,
            item.value("description", ""),
            item.value("excerpt", ""),
            item.value("image", ""),
            item.value("canonical", ""),
            site_ctx.value("base_url", ""),
            site_ctx.value("twitter_handle", ""),
            item.value("tldr", ""));
        if (cfg.json_ld_enabled || cfg.citation_tags_enabled) {
            nlohmann::json schema_page = item;
            schema_page["url"] = item_url;
            std::string extra;
            if (cfg.json_ld_enabled) {
                extra += modules::seo_schema::build_json_ld(cfg, schema_page, pages_array);
            }
            if (cfg.citation_tags_enabled) {
                extra += modules::seo_schema::build_citation_tags(cfg, schema_page);
            }
            ctx["seo_meta"] = ctx["seo_meta"].get<std::string>() + extra;
        }

        std::string html;
        try {
            html = renderer.render(ds.template_name, ctx, ds.file);
        } catch (const RenderError& e) {
            if (errors) {
                errors->push_back({BuildError::Type::Template, e.source_file(),
                                   e.template_name(), e.line(), 0, e.what()});
            }
            continue;
        } catch (const std::runtime_error& e) {
            if (errors) {
                errors->push_back({BuildError::Type::Generic, ds.file,
                                   ds.template_name, 0, 0, e.what()});
            }
            continue;
        }
        outputs.push_back({output_path, html});

        PageRecord rec;
        rec.output_path = output_path;
        rec.url = item_url;
        rec.deps = {data_file_key, template_key};
        records.push_back(std::move(rec));

        pages_built++;
    }
}

// --- 404 page helper ---

static std::string generate_builtin_404(const Config& cfg) {
    std::ostringstream html;
    html << "<!DOCTYPE html>\n";
    html << "<html lang=\"" << cfg.site_language << "\">\n";
    html << "<head>\n";
    html << "  <meta charset=\"UTF-8\">\n";
    html << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    html << "  <title>404 — Page Not Found</title>\n";
    html << "  <style>\n";
    html << "    body { font-family: -apple-system, sans-serif; text-align: center; "
         << "padding: 4rem 1rem; color: #333; }\n";
    html << "    h1 { font-size: 4rem; margin-bottom: 0.5rem; }\n";
    html << "    p { font-size: 1.2rem; color: #666; }\n";
    html << "    a { color: #0066cc; }\n";
    html << "  </style>\n";
    html << "</head>\n";
    html << "<body>\n";
    html << "  <h1>404</h1>\n";
    html << "  <p>Page not found.</p>\n";
    html << "  <p><a href=\"/\">Return home</a></p>\n";
    html << "</body>\n";
    html << "</html>\n";
    return html.str();
}

// --- Main build pipeline ---

// Run a build hook script. Returns true on success, false on failure.
// If the script path is empty or the file doesn't exist, returns true (warns for missing file).
static bool run_hook(const std::string& script, const std::string& env,
                     const std::string& output_dir, int pages_built) {
    if (script.empty()) return true;

    if (!fs::exists(script)) {
        std::cerr << utils::warning_label() << " hook script '" << script
                  << "' not found — skipping\n";
        return true;
    }

    // Set environment variables for the hook
#ifdef _WIN32
    _putenv_s("CSTATIC_ENV", env.c_str());
    _putenv_s("CSTATIC_OUTPUT_DIR", output_dir.c_str());
    _putenv_s("CSTATIC_PAGES_BUILT", std::to_string(pages_built).c_str());
#else
    setenv("CSTATIC_ENV", env.c_str(), 1);
    setenv("CSTATIC_OUTPUT_DIR", output_dir.c_str(), 1);
    setenv("CSTATIC_PAGES_BUILT", std::to_string(pages_built).c_str(), 1);
#endif

    int rc = std::system(script.c_str());
    return rc == 0;
}

BuildResult build_site(const Config& cfg, bool full_rebuild, bool include_drafts, int jobs) {
    auto start = std::chrono::high_resolution_clock::now();
    BuildResult result;

    // --- Run before_build hook ---
    if (!run_hook(cfg.hook_before_build, cfg.env, cfg.output_dir, 0)) {
        result.errors.push_back({
            BuildError::Type::Generic, "", "before_build", 0, 0,
            "before_build hook failed: '" + cfg.hook_before_build + "'"
        });
        auto end = std::chrono::high_resolution_clock::now();
        result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }

    bool incremental = cfg.incremental_enabled && !full_rebuild;

    // --- Initialize hash store ---
    HashStore hashes(cfg.incremental_hash_file);
    if (incremental) {
        hashes.load();
    }

    // --- Hash all inputs ---
    // Config file
    std::string config_key = "config:config.toml";
    {
        std::string config_contents = utils::read_file("config.toml");
        hashes.hash_string(config_key, config_contents);
    }

    // If config changed and incremental, force full rebuild
    if (incremental && !hashes.is_unchanged_key(config_key)) {
        incremental = false;
        // Re-initialize without previous hashes to force clean rebuild
        hashes = HashStore(cfg.incremental_hash_file);
        hashes.hash_string(config_key, utils::read_file("config.toml"));
    }

    // Source markdown files
    auto md_files = collect_files(cfg.source_dir, ".md");
    // Author entity files (G6) are NOT content pages — they're loaded into
    // the AuthorsIndex and rendered as profile pages separately. Excluding
    // them here prevents a URL collision with the generated
    // /<authors_dir>/<slug>/ pages.
    if (cfg.authors_enabled && !cfg.authors_dir.empty()) {
        fs::path authors_path(cfg.authors_dir);
        md_files.erase(std::remove_if(md_files.begin(), md_files.end(),
            [&authors_path](const std::string& f) {
                try {
                    std::string s = fs::relative(f, authors_path).string();
                    return !s.empty() && s.compare(0, 2, "..") != 0;
                } catch (...) {
                    return false;
                }
            }), md_files.end());
    }
    for (const auto& f : md_files) {
        hashes.hash_file(f);
    }

    // Template files
    auto tmpl_files = collect_template_files(cfg.template_dir);
    for (const auto& f : tmpl_files) {
        hashes.hash_file(f);
    }

    // Data files
    auto data_files = collect_data_files(cfg.data_dir);
    for (const auto& f : data_files) {
        hashes.hash_file(f);
    }

    // Shortcode templates — changes here must invalidate dependent pages.
    if (!cfg.shortcodes_dir.empty()) {
        for (const auto& f : collect_files(cfg.shortcodes_dir, ".html")) {
            hashes.hash_file(f);
        }
    }

    // --- Load data ---
    DataLoader data_loader(cfg.data_dir);
    nlohmann::json all_data = data_loader.load_all();

    // --- Set up renderer and site context ---
    TemplateRenderer renderer(cfg.template_dir);

    // Pre-compute asset manifest if fingerprinting is enabled.
    // This must happen before rendering so {{ asset() }} resolves correctly.
    AssetManifest asset_manifest;
    if (cfg.fingerprint_assets) {
        asset_manifest = build_asset_manifest(cfg);
        renderer.set_asset_manifest(asset_manifest.entries);
    }

    nlohmann::json site_ctx;
    site_ctx["title"] = cfg.site_title;
    site_ctx["base_url"] = cfg.site_base_url;
    site_ctx["language"] = cfg.site_language;
    site_ctx["twitter_handle"] = cfg.site_twitter_handle;
    site_ctx["env"] = cfg.env;

    // --- Load authors index (G6) ---
    // When enabled, src/authors/*.md become first-class entities resolvable
    // from page frontmatter `author: <slug>`. The index is built once here
    // (single-threaded) and read-only in the multi-threaded render loop.
    AuthorsIndex authors_index;
    // URL path prefix for author profile pages, derived from authors_dir's
    // basename (e.g. "src/authors" -> "/authors/").
    std::string authors_url_base;
    if (cfg.authors_enabled) {
        authors_index.load(cfg.authors_dir);
        std::string dir_base = fs::path(cfg.authors_dir).filename().string();
        if (dir_base.empty()) dir_base = "authors";
        authors_url_base = "/" + dir_base + "/";
        // Expose the full author roster to every template via {{ site.authors }}.
        if (!authors_index.empty()) {
            nlohmann::json authors_map = nlohmann::json::object();
            for (const auto& slug : authors_index.all_slugs()) {
                authors_map[slug] = authors_index.context(slug);
                authors_map[slug]["url"] = cfg.site_base_url + authors_url_base + slug + "/";
            }
            site_ctx["authors"] = authors_map;
        }
    }

    // G10: expose the resolved Organization object as {{ org }} so templates
    // can render consistent footers/cards from a single source of truth.
    site_ctx["org"] = modules::seo_schema::build_org_context(cfg);

    // G10: validate organization identity once per build (non-fatal warnings).
    if (!cfg.org_name.empty()) {
        std::vector<std::string> known_slugs;
        if (cfg.authors_enabled) {
            for (const auto& s : authors_index.all_slugs()) known_slugs.push_back(s);
        }
        for (const auto& issue : modules::seo_schema::validate_organization(cfg, known_slugs)) {
            std::cerr << utils::warning_label() << " org '" << issue.field
                      << "': " << issue.message << "\n";
        }
    }

    // Markdown rendering options (syntax highlighting + GFM extensions).
    MarkdownOptions md_opts;
    md_opts.highlight_enabled = cfg.highlight_enabled;
    md_opts.highlight_style   = cfg.highlight_style;
    md_opts.extensions        = cfg.markdown_extensions;

    // Shortcode processor — no-op when the shortcodes directory is empty or
    // missing (see ShortcodeProcessor::available()).
    ShortcodeProcessor shortcode_processor(cfg.shortcodes_dir);

    // Schema-block processor ({% schema "Type" %}...{% endschema %}) — pure
    // parsing, no state. A no-op pass-through on bodies with no schema blocks.
    SchemaBlockProcessor schema_block_processor;

    // Wikilink resolver — built across Phase 1a (indexing) and read-only in
    // Phase 2 (backlinks). Single-threaded population; multi-threaded reads.
    LinkGraph link_graph;

    // --- Phase 1: Parse all markdown pages ---
    struct RawPage {
        std::string source_path;
        std::string url;
        std::string output_path;
        ParsedContent parsed;
        std::string html_content;
        std::string template_path; // resolved template file path
        int collection_idx = -1;   // index into cfg.collections (-1 = none)
        std::vector<Wikilink> outgoing_links;  // populated in 1b when wikilinks are on
        bool render_failed = false;            // markdown render error in 1b
    };

    std::vector<RawPage> raw_pages;
    nlohmann::json pages_array = nlohmann::json::array();

    // Collect alias redirects: alias_url → target page url + date.
    struct AliasEntry {
        std::string alias_url;
        std::string target_url;
        std::string date;
    };
    std::vector<AliasEntry> alias_entries;

    for (const auto& file_path : md_files) {
        RawPage rp;
        rp.source_path = file_path;

        std::string content;
        try {
            content = utils::read_file(file_path);
            rp.parsed = parse_frontmatter(content, file_path);
        } catch (const FrontmatterError& e) {
            result.errors.push_back({BuildError::Type::Frontmatter, e.source_file(),
                                     "", e.line(), e.column(), e.what()});
            continue;
        } catch (const std::exception& e) {
            result.errors.push_back({BuildError::Type::Frontmatter, file_path,
                                     "", 0, 0, e.what()});
            continue;
        }

        if (rp.parsed.frontmatter.draft && !include_drafts) {
            result.pages_skipped++;
            continue;
        }

        // Scheduled publishing: skip pages dated in the future unless opted in.
        // `include_drafts` (dev server / --drafts) also bypasses scheduling so
        // authors can preview upcoming content locally.
        if (!cfg.publish_future && !include_drafts && !rp.parsed.frontmatter.date.empty()) {
            std::tm tm = {};
            std::istringstream ss(rp.parsed.frontmatter.date);
            ss >> std::get_time(&tm, "%Y-%m-%d");
            if (!ss.fail() && std::mktime(&tm) > std::time(nullptr)) {
                result.pages_scheduled++;
                continue;
            }
        }

        if (!rp.parsed.frontmatter.permalink.empty()) {
            rp.url = rp.parsed.frontmatter.permalink;
            if (rp.url.front() != '/') rp.url = "/" + rp.url;
            if (rp.url.back() != '/') rp.url += "/";
        } else {
            rp.url = utils::source_to_url(file_path, cfg.source_dir);
        }

        // Special case: src/404.md → output/404.html (not output/404/index.html)
        bool is_404 = (fs::path(file_path).stem().string() == "404");
        if (is_404) {
            rp.url = "/404.html";
            rp.output_path = cfg.output_dir + "/404.html";
        } else {
            rp.output_path = utils::url_to_output(rp.url, cfg.output_dir);
        }

        // Resolve title (with filename fallback) before rendering — shortcodes
        // may reference {{ page.title }} during body expansion.
        std::string title = rp.parsed.frontmatter.title;
        if (title.empty()) {
            auto fname = fs::path(file_path).stem().string();
            if (fname == "index") {
                fname = fs::path(file_path).parent_path().filename().string();
            }
            for (auto& c : fname) {
                if (c == '-' || c == '_') c = ' ';
            }
            if (!fname.empty()) fname[0] = std::toupper(fname[0]);
            title = fname;
        }

        // Resolve template path
        std::string layout = rp.parsed.frontmatter.layout;
        std::string tmpl_path = utils::path_join(cfg.template_dir, layout + ".html");
        if (!fs::exists(tmpl_path)) {
            tmpl_path = ""; // will use built-in fallback
        }
        rp.template_path = tmpl_path;

        // Match page to a collection by checking if source_path is under source_dir/<collection.name>/
        for (size_t ci = 0; ci < cfg.collections.size(); ci++) {
            const auto& col = cfg.collections[ci];
            std::string col_prefix = utils::path_join(cfg.source_dir, col.name) + "/";
            if (rp.source_path.size() > col_prefix.size() &&
                rp.source_path.substr(0, col_prefix.size()) == col_prefix) {
                rp.collection_idx = static_cast<int>(ci);

                // Override template if no explicit layout in frontmatter
                if (rp.parsed.frontmatter.layout == "default") {
                    rp.parsed.frontmatter.layout = col.template_;
                    std::string col_tmpl_path = utils::path_join(cfg.template_dir, col.template_ + ".html");
                    rp.template_path = fs::exists(col_tmpl_path) ? col_tmpl_path : "";
                }

                // Override URL if collection has url_pattern
                if (!col.url_pattern.empty()) {
                    std::string slug = fs::path(file_path).stem().string();
                    nlohmann::json slug_obj;
                    slug_obj["slug"] = slug;
                    rp.url = interpolate_url(col.url_pattern, slug_obj);
                    if (!rp.url.empty() && rp.url.front() != '/') rp.url = "/" + rp.url;
                    if (!rp.url.empty() && rp.url.back() != '/') rp.url += "/";
                    rp.output_path = utils::url_to_output(rp.url, cfg.output_dir);
                }

                break;
            }
        }

        // Persist the resolved title on the parsed frontmatter so Phase 1b
        // can read it back from rp.parsed.frontmatter.title without redoing
        // the fallback logic.
        rp.parsed.frontmatter.title = title;

        // Wikilinks: index this page after URL and title are final so other
        // pages' [[...]] targets can resolve by filename stem, lowercase
        // title, or frontmatter alias.
        if (cfg.wikilinks_enabled) {
            link_graph.index_page(rp.url, title, rp.parsed.frontmatter.aliases);
        }

        // Collect alias redirects before moving rp.
        for (const auto& alias_url : rp.parsed.frontmatter.aliases) {
            AliasEntry ae;
            ae.alias_url = alias_url;
            ae.target_url = rp.url;
            ae.date = rp.parsed.frontmatter.date;
            alias_entries.push_back(std::move(ae));
        }

        raw_pages.push_back(std::move(rp));
    }

    // --- Phase 1a complete: link graph is fully indexed. Hash its
    // serialized form so any title/alias/stem change invalidates downstream
    // pages on the next incremental build (coarse-grained, like shortcode
    // template changes — see needs_rebuild check in Phase 2).
    if (cfg.wikilinks_enabled) {
        hashes.hash_string("meta:wikilinks_index", link_graph.serialize_index());
    }
    // Authors index — any author file edit (name, bio, added/removed) must
    // invalidate every page so {{ page.author }} and Person schemas refresh.
    if (cfg.authors_enabled) {
        nlohmann::json ah = nlohmann::json::array();
        for (const auto& slug : authors_index.all_slugs()) {
            ah.push_back(slug + ":" + authors_index.context(slug).dump());
        }
        hashes.hash_string("meta:authors_index", ah.dump());
    }

    // --- Phase 1b: Shortcodes, wikilinks, markdown render ---
    // Bodies are rendered AFTER every page has been indexed so [[wikilinks]]
    // can resolve against the complete site graph. pages_array is populated
    // here (excerpt depends on html_content) and consumed by downstream
    // modules (sitemap, RSS, search, OG images).
    for (auto& rp : raw_pages) {
        const std::string& title = rp.parsed.frontmatter.title;

        // Build a lightweight page context once — shared by the shortcode and
        // schema-block processors below.
        std::string body = rp.parsed.body;
        nlohmann::json page_ctx;
        page_ctx["title"] = title;
        page_ctx["url"]   = rp.url;
        page_ctx["slug"]  = fs::path(rp.source_path).stem().string();
        if (!rp.parsed.frontmatter.date.empty())
            page_ctx["date"] = rp.parsed.frontmatter.date;

        // Expand shortcodes ({{< name params >}} / {{< name >}}...{{< /name >}})
        // before cmark-gfm. CMARK_OPT_UNSAFE lets the emitted raw HTML pass
        // through to the final document.
        if (shortcode_processor.available()) {
            body = shortcode_processor.process(body, page_ctx);
        }

        // Schema blocks ({% schema "Type" attrs %}...{% endschema %}): emit
        // visible HTML into the body and collect JSON-LD objects. Runs after
        // shortcodes (so a shortcode may produce a schema block) and before
        // wikilinks / render_markdown so the emitted raw HTML survives. The
        // collected schemas are appended to frontmatter.schema_extra, which
        // seo_schema::build_json_ld (G3) emits verbatim.
        {
            std::vector<nlohmann::json> page_schemas;
            body = schema_block_processor.process(body, page_schemas, page_ctx);
            if (!page_schemas.empty()) {
                auto& custom = rp.parsed.frontmatter.custom;
                if (!custom.contains("schema_extra") || !custom["schema_extra"].is_array()) {
                    custom["schema_extra"] = nlohmann::json::array();
                }
                for (auto& s : page_schemas) {
                    custom["schema_extra"].push_back(std::move(s));
                }
            }
        }

        // G5: Standalone `##?` FAQ extraction. Questions left in the body
        // after schema blocks (G4) are standalone Q&A. Render visible
        // <details>, expose page.faq, and merge a FAQPage into schema_extra
        // (combining with any G4 FAQPage) so seo_schema::build_json_ld (G3)
        // emits one merged FAQPage. Runs before wikilinks / render_markdown so
        // the emitted raw HTML survives into the final document.
        {
            auto sfaq = process_standalone_faq(body);
            if (sfaq.found) {
                body = sfaq.body;
                auto& custom = rp.parsed.frontmatter.custom;
                custom["faq"] = sfaq.faq_ctx;
                if (!custom.contains("schema_extra") || !custom["schema_extra"].is_array()) {
                    custom["schema_extra"] = nlohmann::json::array();
                }
                merge_faq_into_schema_extra(custom["schema_extra"], sfaq.questions);
            }
        }

        // Rewrite [[wikilinks]] -> <a href>. Done AFTER shortcodes so a
        // shortcode body can introduce wikilinks, and BEFORE render_markdown
        // so the emitted raw HTML survives into the final document.
        if (cfg.wikilinks_enabled) {
            rp.outgoing_links = link_graph.rewrite_wikilinks(body, rp.url);
            link_graph.add_outgoing(rp.url, rp.outgoing_links);
        }

        try {
            rp.html_content = render_markdown(body, md_opts);
        } catch (const std::exception& e) {
            result.errors.push_back({BuildError::Type::Markdown, rp.source_path,
                                     "", 0, 0, e.what()});
            rp.render_failed = true;
            continue;
        }

        // G8: Passage index. Extract citable passages from rendered HTML so
        // templates get {{ page.passages }} and seo_schema emits JSON-LD
        // hasPart. Always on (pure derived data, like excerpt). JSON-LD
        // emission is gated by the existing json_ld_enabled flag in seo_schema.
        {
            auto passages = extract_passages(rp.html_content);
            if (!passages.empty()) {
                rp.parsed.frontmatter.custom["passages"] = to_json(passages);
            }
        }

        // G11: Auto Table of Contents. Inject id="..." attributes into
        // headings (so #anchor links resolve — cmark-gfm doesn't emit them)
        // and build a {{ page.toc }} tree. IDs use the same slugify as G8
        // so passage hasPart URLs and heading anchors stay in sync.
        // Also replaces <!--toc--> markers with rendered nav HTML.
        // Always on (pure derived data). Runs AFTER render_markdown and
        // AFTER G8 so passage IDs are already computed from the same HTML.
        {
            auto toc = build_toc(rp.html_content);
            if (!toc.empty()) {
                rp.parsed.frontmatter.custom["toc"] = to_json(toc);
                replace_toc_markers(rp.html_content, toc);
            }
        }

        // G12: Reading time / word count / difficulty. Cheap computed
        // fields exposed as {{ page.word_count }}, {{ page.reading_time }},
        // {{ page.difficulty }}; seo_schema emits wordCount + timeRequired
        // on Article-typed JSON-LD. Always on (pure derived data, like
        // excerpt/passages/toc). Runs AFTER render_markdown so the HTML
        // is final (code blocks stripped before counting — code isn't prose).
        {
            auto rd = compute_readability(rp.html_content);
            rp.parsed.frontmatter.custom["word_count"]   = rd.word_count;
            rp.parsed.frontmatter.custom["reading_time"] = rd.reading_time_min;
            rp.parsed.frontmatter.custom["difficulty"]   = rd.difficulty;
        }

        nlohmann::json tags = nlohmann::json::array();
        for (const auto& tag : rp.parsed.frontmatter.tags) {
            tags.push_back(tag);
        }

        nlohmann::json page_meta;
        page_meta["title"] = title;
        page_meta["url"] = rp.url;
        page_meta["date"] = rp.parsed.frontmatter.date;
        page_meta["tags"] = tags;
        page_meta["excerpt"] = utils::truncate_text(utils::strip_html_tags(rp.html_content), 200);
        page_meta["content_html"]       = rp.html_content;
        page_meta["description"]        = rp.parsed.frontmatter.description;
        page_meta["image"]              = rp.parsed.frontmatter.image;
        page_meta["canonical"]          = rp.parsed.frontmatter.canonical;
        page_meta["sitemap_changefreq"] = rp.parsed.frontmatter.sitemap_changefreq;
        page_meta["sitemap_priority"]   = rp.parsed.frontmatter.sitemap_priority;
        pages_array.push_back(page_meta);
    }

    // Drop pages that failed markdown rendering so Phase 2 doesn't try to
    // render a layout template for them. Matches the pre-split behavior
    // where `continue` in the original loop excluded them from raw_pages.
    raw_pages.erase(
        std::remove_if(raw_pages.begin(), raw_pages.end(),
                       [](const RawPage& p) { return p.render_failed; }),
        raw_pages.end());

    std::sort(pages_array.begin(), pages_array.end(),
        [](const nlohmann::json& a, const nlohmann::json& b) {
            return a.value("date", "") > b.value("date", "");
        });

    // --- Hash pages_array so structural changes (added/deleted page, title
    // or date edits that shift sort order) invalidate every page that
    // references {{ pages }}. File-level hashing can't detect these because
    // the changed file is one page while the dependent template is another.
    // Coarse-grained like meta:wikilinks_index — any structural change
    // invalidates the whole site. See needs_rebuild check in Phase 2.
    //
    // content_html is excluded so ordinary body-text edits don't pull every
    // {{ pages }}-referencing page into an incremental rebuild (the JSON Feed
    // module reads content_html directly at generation time; templates that
    // render {{ p.content_html }} are rare and covered by their own file-hash
    // dependency on the source markdown).
    {
        nlohmann::json pages_for_hash = nlohmann::json::array();
        for (const auto& p : pages_array) {
            nlohmann::json p_copy = p;
            p_copy.erase("content_html");
            pages_for_hash.push_back(p_copy);
        }
        hashes.hash_string("meta:pages_array", pages_for_hash.dump());
    }

    // --- Phase 1.5: Apply markdown pagination rules ---
    std::vector<CachedOutput> all_outputs;
    std::vector<PageRecord> all_records;
    // For each pagination rule, collect matching pages and generate paginated index pages.
    {
        for (const auto& rule : cfg.pagination_rules) {
            // Find pages matching the source prefix
            std::string source_prefix = "/" + rule.source + "/";
            nlohmann::json matching = nlohmann::json::array();
            for (const auto& p : pages_array) {
                std::string url = p.value("url", "");
                if (url.size() >= source_prefix.size() &&
                    url.substr(0, source_prefix.size()) == source_prefix) {
                    matching.push_back(p);
                }
            }

            if (matching.empty()) continue;

            int total_items = static_cast<int>(matching.size());
            int per_page = rule.per_page > 0 ? rule.per_page : 10;
            int total_pages = (total_items + per_page - 1) / per_page;
            std::string base_url = source_prefix;

            for (int page = 0; page < total_pages; page++) {
                int start = page * per_page;
                int end = std::min(start + per_page, total_items);

                nlohmann::json page_items = nlohmann::json::array();
                for (int i = start; i < end; i++) {
                    page_items.push_back(matching[i]);
                }

                nlohmann::json pagination;
                pagination["page"] = page + 1;
                pagination["total_pages"] = total_pages;
                pagination["total_items"] = total_items;
                pagination["per_page"] = per_page;
                pagination["items"] = page_items;
                pagination["prev_url"] = "";
                pagination["next_url"] = "";

                if (page > 0) {
                    pagination["prev_url"] = (page == 1) ? base_url
                        : base_url + "page/" + std::to_string(page) + "/";
                }
                if (page < total_pages - 1) {
                    pagination["next_url"] = base_url + "page/" + std::to_string(page + 2) + "/";
                }

                std::string page_url = (page == 0) ? base_url
                    : base_url + "page/" + std::to_string(page + 1) + "/";
                std::string output_path = utils::url_to_output(page_url, cfg.output_dir);

                // Pre-load the pagination template
                renderer.preload_template(rule.template_);

                nlohmann::json ctx;
                ctx["site"] = site_ctx;
                ctx["pages"] = pages_array;
                ctx["pagination"] = pagination;
                ctx["page"] = nlohmann::json::object();
                ctx["page"]["url"] = page_url;
                ctx["page"]["title"] = rule.source;
                ctx["page"]["content"] = "";

                std::string html = renderer.render(rule.template_, ctx);

                CachedOutput out;
                out.output_path = output_path;
                out.html = html;

                PageRecord rec;
                rec.output_path = output_path;
                rec.url = page_url;

                all_outputs.push_back(std::move(out));
                all_records.push_back(std::move(rec));

                result.pages_built++;
            }
        }
    }

    // --- Phase 1.6: Generate collection index pages ---
    {
        // Group raw_pages by collection_idx
        for (size_t ci = 0; ci < cfg.collections.size(); ci++) {
            const auto& col = cfg.collections[ci];

            // Collect page_meta for pages in this collection
            nlohmann::json col_pages = nlohmann::json::array();
            for (const auto& rp : raw_pages) {
                if (rp.collection_idx != static_cast<int>(ci)) continue;

                nlohmann::json pm;
                pm["title"] = rp.parsed.frontmatter.title;
                pm["url"] = rp.url;
                pm["date"] = rp.parsed.frontmatter.date;

                nlohmann::json tags = nlohmann::json::array();
                for (const auto& tag : rp.parsed.frontmatter.tags) {
                    tags.push_back(tag);
                }
                pm["tags"] = tags;
                pm["excerpt"] = utils::truncate_text(utils::strip_html_tags(rp.html_content), 200);
                col_pages.push_back(pm);
            }

            if (col_pages.empty()) continue;

            // Sort by the collection's sort_by field
            bool desc = (col.sort_order == "desc");
            std::sort(col_pages.begin(), col_pages.end(),
                [&col, desc](const nlohmann::json& a, const nlohmann::json& b) {
                    std::string va = a.value(col.sort_by, "");
                    std::string vb = b.value(col.sort_by, "");
                    return desc ? (va > vb) : (va < vb);
                });

            // Render the collection index page
            std::string index_url = "/" + col.name + "/";
            std::string index_output = utils::url_to_output(index_url, cfg.output_dir);

            nlohmann::json collection_ctx;
            collection_ctx["name"] = col.name;
            collection_ctx["pages"] = col_pages;

            nlohmann::json ctx;
            ctx["site"] = site_ctx;
            ctx["pages"] = pages_array;
            ctx["collection"] = collection_ctx;
            ctx["page"] = nlohmann::json::object();
            ctx["page"]["url"] = index_url;
            ctx["page"]["title"] = col.name;
            ctx["page"]["content"] = "";

            renderer.preload_template(col.index_template);
            std::string html = renderer.render(col.index_template, ctx);

            CachedOutput out;
            out.output_path = index_output;
            out.html = html;
            all_outputs.push_back(std::move(out));

            PageRecord rec;
            rec.output_path = index_output;
            rec.url = index_url;
            all_records.push_back(std::move(rec));

            result.pages_built++;
        }
    }

    // --- Phase 1.7: Generate taxonomy pages ---
    {
        for (const auto& tax : cfg.taxonomies) {
            // Build map: term -> array of page_meta
            std::map<std::string, nlohmann::json> term_pages;

            for (const auto& rp : raw_pages) {
                // Extract the taxonomy key from frontmatter
                // For "tags", look at rp.parsed.frontmatter.tags
                // For custom fields, look at custom frontmatter
                std::vector<std::string> terms;

                if (tax.key == "tags") {
                    terms = rp.parsed.frontmatter.tags;
                } else {
                    // Look in custom frontmatter
                    if (rp.parsed.frontmatter.custom.contains(tax.key)) {
                        auto& val = rp.parsed.frontmatter.custom[tax.key];
                        if (val.is_string()) {
                            terms.push_back(val.get<std::string>());
                        } else if (val.is_array()) {
                            for (const auto& item : val) {
                                if (item.is_string()) {
                                    terms.push_back(item.get<std::string>());
                                }
                            }
                        }
                    }
                }

                for (const auto& term : terms) {
                    nlohmann::json pm;
                    pm["title"] = rp.parsed.frontmatter.title;
                    pm["url"] = rp.url;
                    pm["date"] = rp.parsed.frontmatter.date;
                    pm["excerpt"] = utils::truncate_text(utils::strip_html_tags(rp.html_content), 200);

                    nlohmann::json tags_json = nlohmann::json::array();
                    for (const auto& tag : rp.parsed.frontmatter.tags) {
                        tags_json.push_back(tag);
                    }
                    pm["tags"] = tags_json;

                    term_pages[term].push_back(pm);
                }
            }

            if (term_pages.empty()) continue;

            // Generate term pages: /<key>/<term>/
            for (const auto& [term, pages] : term_pages) {
                std::string term_slug = term;
                std::transform(term_slug.begin(), term_slug.end(), term_slug.begin(),
                    [](unsigned char c) { return std::tolower(c == ' ' ? '-' : c); });

                std::string term_url = "/" + tax.key + "/" + term_slug + "/";
                std::string term_output = utils::url_to_output(term_url, cfg.output_dir);

                nlohmann::json tax_ctx;
                tax_ctx["key"] = tax.key;
                tax_ctx["term"] = term;
                tax_ctx["pages"] = pages;

                nlohmann::json ctx;
                ctx["site"] = site_ctx;
                ctx["pages"] = pages_array;
                ctx["taxonomy"] = tax_ctx;
                ctx["page"] = nlohmann::json::object();
                ctx["page"]["url"] = term_url;
                ctx["page"]["title"] = term;

                renderer.preload_template(tax.template_);
                std::string html = renderer.render(tax.template_, ctx);

                CachedOutput out;
                out.output_path = term_output;
                out.html = html;
                all_outputs.push_back(std::move(out));

                PageRecord rec;
                rec.output_path = term_output;
                rec.url = term_url;
                all_records.push_back(std::move(rec));

                result.pages_built++;
            }

            // Generate taxonomy index page: /<key>/
            nlohmann::json terms_array = nlohmann::json::array();
            for (const auto& [term, pages] : term_pages) {
                std::string term_slug = term;
                std::transform(term_slug.begin(), term_slug.end(), term_slug.begin(),
                    [](unsigned char c) { return std::tolower(c == ' ' ? '-' : c); });

                nlohmann::json term_obj;
                term_obj["term"] = term;
                term_obj["count"] = pages.size();
                term_obj["url"] = "/" + tax.key + "/" + term_slug + "/";
                terms_array.push_back(term_obj);
            }

            std::string index_url = "/" + tax.key + "/";
            std::string index_output = utils::url_to_output(index_url, cfg.output_dir);

            nlohmann::json tax_index_ctx;
            tax_index_ctx["key"] = tax.key;
            tax_index_ctx["terms"] = terms_array;

            nlohmann::json ctx;
            ctx["site"] = site_ctx;
            ctx["pages"] = pages_array;
            ctx["taxonomy"] = tax_index_ctx;
            ctx["page"] = nlohmann::json::object();
            ctx["page"]["url"] = index_url;
            ctx["page"]["title"] = tax.key;

            renderer.preload_template(tax.index_template);
            std::string index_html = renderer.render(tax.index_template, ctx);

            CachedOutput out;
            out.output_path = index_output;
            out.html = index_html;
            all_outputs.push_back(std::move(out));

            PageRecord rec;
            rec.output_path = index_output;
            rec.url = index_url;
            all_records.push_back(std::move(rec));

            result.pages_built++;
        }
    }

    // --- Phase 1.8: Generate author profile pages (G6) ---
    // One page per loaded author at /<authors_dir_basename>/<slug>/, rendered
    // with the "author" template. The page carries a Person JSON-LD schema
    // (via schema_extra) so AI engines can attribute authorship.
    if (cfg.authors_enabled && !authors_index.empty()) {
        std::string author_tpl = (fs::path(cfg.template_dir) / "author.html").string();
        if (!fs::exists(author_tpl)) {
            std::cerr << utils::warning_label()
                      << "authors.enabled is on but templates/author.html is missing — "
                         "skipping author profile page generation\n";
        } else {
            // Group published pages by author slug (one pass over raw_pages).
            std::map<std::string, nlohmann::json> posts_by_author;
            for (const auto& rp : raw_pages) {
                const auto& custom = rp.parsed.frontmatter.custom;
                if (!custom.contains("author") || !custom["author"].is_string()) continue;
                std::string a_slug = custom["author"].get<std::string>();
                if (!authors_index.has(a_slug)) continue;

                nlohmann::json pm;
                pm["title"]   = rp.parsed.frontmatter.title;
                pm["url"]     = rp.url;
                pm["date"]    = rp.parsed.frontmatter.date;
                pm["excerpt"] = utils::truncate_text(utils::strip_html_tags(rp.html_content), 200);
                nlohmann::json tags_json = nlohmann::json::array();
                for (const auto& tag : rp.parsed.frontmatter.tags) tags_json.push_back(tag);
                pm["tags"] = tags_json;
                posts_by_author[a_slug].push_back(pm);
            }

            renderer.preload_template("author");
            std::string author_source_prefix = cfg.authors_dir + "/";
            for (const auto& slug : authors_index.all_slugs()) {
                std::string author_url = authors_url_base + slug + "/";
                std::string author_full_url = cfg.site_base_url + author_url;
                std::string output_path = utils::url_to_output(author_url, cfg.output_dir);

                nlohmann::json author_ctx = authors_index.context(slug);
                author_ctx["url"] = author_full_url;
                auto pit = posts_by_author.find(slug);
                author_ctx["posts"] = (pit != posts_by_author.end())
                    ? pit->second : nlohmann::json::array();

                std::string author_name = author_ctx.value("name", slug);
                std::string author_bio  = author_ctx.value("bio", "");

                nlohmann::json ctx;
                ctx["site"]   = site_ctx;
                ctx["pages"]  = pages_array;
                ctx["author"] = author_ctx;
                ctx["page"] = nlohmann::json::object();
                ctx["page"]["url"]   = author_url;
                ctx["page"]["title"] = author_name;
                ctx["page"]["type"]  = "ProfilePage";

                std::string seo_meta = build_seo_meta(
                    author_name, author_url, author_bio, "", "", "",
                    cfg.site_base_url, cfg.site_twitter_handle);
                if (cfg.json_ld_enabled) {
                    nlohmann::json schema_page;
                    schema_page["title"]       = author_name;
                    schema_page["url"]         = author_url;
                    schema_page["description"] = author_bio;
                    schema_page["type"]        = "ProfilePage";
                    // Person schema via schema_extra — emitted verbatim by G3.
                    nlohmann::json person = authors_index.person_schema(slug, author_full_url);
                    schema_page["schema_extra"] = nlohmann::json::array({ person });
                    seo_meta += modules::seo_schema::build_json_ld(cfg, schema_page, pages_array);
                }
                ctx["seo_meta"] = seo_meta;

                std::string html;
                std::string source_label = author_source_prefix + slug + ".md";
                try {
                    html = renderer.render("author", ctx, source_label);
                } catch (const RenderError& e) {
                    result.errors.push_back({BuildError::Type::Template, e.source_file(),
                                             e.template_name(), e.line(), 0, e.what()});
                    continue;
                } catch (const std::runtime_error& e) {
                    result.errors.push_back({BuildError::Type::Generic, source_label,
                                             "author", 0, 0, e.what()});
                    continue;
                }

                CachedOutput out;
                out.output_path = output_path;
                out.html = html;
                all_outputs.push_back(std::move(out));

                PageRecord rec;
                rec.output_path = output_path;
                rec.url = author_url;
                all_records.push_back(std::move(rec));

                result.pages_built++;
            }
        }
    }

    // --- OG image URLs (computed before rendering so og:image lands in seo_meta) ---
    // The image files themselves are written later (after the output-write section)
    // so the full-rebuild wipe doesn't delete them.
    std::unordered_map<std::string, std::string> og_image_map;
    if (cfg.og_images_enabled) {
        for (auto& p : pages_array) {
            std::string p_url = p.value("url", "");
            if (p_url.empty() || p.value("title", "").empty()) continue;
            std::string og = modules::og_image_url_for(cfg, p_url);
            p["og_image"] = og;
            og_image_map[p_url] = og;
        }
    }

    auto t_phase1 = std::chrono::high_resolution_clock::now();

    // --- Phase 2: Render markdown pages, track dependencies, decide what to rebuild ---

    // Shortcode template paths — every rendered page depends on them so a
    // template change invalidates the whole site. (Coarse but simple: the
    // hash store dedups, and most sites have only a handful of shortcodes.)
    std::vector<std::string> shortcode_deps;
    if (shortcode_processor.available()) {
        shortcode_deps = collect_files(cfg.shortcodes_dir, ".html");
    }

    // Build task list and pre-load templates
    struct RenderTask {
        size_t index;
        bool needs_rebuild;
    };
    std::vector<RenderTask> tasks;

    for (size_t i = 0; i < raw_pages.size(); i++) {
        auto& rp = raw_pages[i];
        // Build dependency list for this page
        std::vector<std::string> deps = {rp.source_path};
        if (!rp.template_path.empty()) {
            deps.push_back(rp.template_path);
        }
        // Include ancestor templates (from {% extends %}) so editing a
        // parent layout rebuilds dependent pages.
        for (const auto& ancestor : renderer.template_ancestors(rp.parsed.frontmatter.layout)) {
            deps.push_back(ancestor);
        }
        deps.insert(deps.end(), shortcode_deps.begin(), shortcode_deps.end());
        // Structural deps: invalidate every page when the site graph changes.
        deps.push_back("meta:pages_array");
        if (cfg.wikilinks_enabled) {
            deps.push_back("meta:wikilinks_index");
        }
        if (cfg.authors_enabled) {
            deps.push_back("meta:authors_index");
        }

        bool needs_rebuild = true;
        if (incremental) {
            needs_rebuild = false;
            for (const auto& dep : deps) {
                if (!hashes.is_unchanged(dep)) {
                    needs_rebuild = true;
                    break;
                }
            }
            // Wikilinks invalidate every page when the resolver index
            // changes (any title/alias/stem edit). Coarse-grained but
            // consistent with how shortcode template changes are handled.
            if (!needs_rebuild && cfg.wikilinks_enabled &&
                !hashes.is_unchanged_key("meta:wikilinks_index")) {
                needs_rebuild = true;
            }
            // Same coarse-grained invalidation for author resolution: an
            // author-file edit must refresh every page's Person schema.
            if (!needs_rebuild && cfg.authors_enabled &&
                !hashes.is_unchanged_key("meta:authors_index")) {
                needs_rebuild = true;
            }
        }

        if (needs_rebuild) {
            renderer.preload_template(rp.parsed.frontmatter.layout);
        }

        tasks.push_back({i, needs_rebuild});
    }

    // Determine thread count
    int thread_count = 1;
    if (jobs > 0) {
        thread_count = jobs;
    } else if (jobs == 0) {
        unsigned hw = std::thread::hardware_concurrency();
        thread_count = hw > 0 ? std::min(static_cast<int>(hw), 4) : 1;
    }

    // Collect tasks that need rendering
    std::vector<size_t> rebuild_indices;
    for (size_t i = 0; i < tasks.size(); i++) {
        if (tasks[i].needs_rebuild) {
            rebuild_indices.push_back(i);
        }
    }

    // Pre-allocate output slots
    size_t N = rebuild_indices.size();
    std::vector<std::string> rendered_html(N);
    std::vector<bool> was_rendered(N, false);

    // Build collections JSON for template context
    nlohmann::json collections_json = nlohmann::json::object();
    for (size_t ci = 0; ci < cfg.collections.size(); ci++) {
        const auto& col = cfg.collections[ci];
        nlohmann::json col_pages = nlohmann::json::array();
        for (const auto& rp : raw_pages) {
            if (rp.collection_idx != static_cast<int>(ci)) continue;
            nlohmann::json pm;
            pm["title"] = rp.parsed.frontmatter.title;
            pm["url"] = rp.url;
            pm["date"] = rp.parsed.frontmatter.date;
            col_pages.push_back(pm);
        }
        bool desc = (col.sort_order == "desc");
        std::sort(col_pages.begin(), col_pages.end(),
            [&col, desc](const nlohmann::json& a, const nlohmann::json& b) {
                std::string va = a.value(col.sort_by, "");
                std::string vb = b.value(col.sort_by, "");
                return desc ? (va > vb) : (va < vb);
            });
        collections_json[col.name] = col_pages;
    }

    std::mutex errors_mutex;

    auto render_task = [&](size_t start, size_t end) {
        for (size_t ti = start; ti < end; ti++) {
            size_t page_idx = rebuild_indices[ti];
            auto& rp = raw_pages[page_idx];

            nlohmann::json ctx;
            ctx["page"] = nlohmann::json::object();
            ctx["page"]["title"] = rp.parsed.frontmatter.title;
            ctx["page"]["url"] = rp.url;
            ctx["page"]["date"] = rp.parsed.frontmatter.date;
            ctx["page"]["content"] = rp.html_content;

            nlohmann::json tags = nlohmann::json::array();
            for (const auto& tag : rp.parsed.frontmatter.tags) {
                tags.push_back(tag);
            }
            ctx["page"]["tags"] = tags;
            for (const auto& [key, val] : rp.parsed.frontmatter.custom.items()) {
                ctx["page"][key] = val;
            }
            if (rp.parsed.frontmatter.draft) {
                ctx["page"]["draft"] = true;
            }

            // Resolve author slug -> full author object (G6). Templates see
            // {{ page.author.name }} etc.; the slug is also remembered so the
            // JSON-LD schema below can emit a Person object.
            std::string author_slug_resolved;
            if (cfg.authors_enabled && !authors_index.empty() &&
                ctx["page"].contains("author") && ctx["page"]["author"].is_string()) {
                const std::string& slug = ctx["page"]["author"].get<std::string>();
                if (!slug.empty()) {
                    if (authors_index.has(slug)) {
                        nlohmann::json ac = authors_index.context(slug);
                        ac["url"] = cfg.site_base_url + authors_url_base + slug + "/";
                        ctx["page"]["author"] = std::move(ac);
                        author_slug_resolved = slug;
                    } else {
                        std::cerr << utils::warning_label() << "author '" << slug
                                  << "' in " << rp.source_path
                                  << " not found in authors index\n";
                    }
                }
            }

            ctx["site"] = site_ctx;
            ctx["pages"] = pages_array;
            ctx["data"] = all_data;
            ctx["collections"] = collections_json;

            // Wikilinks backlinks — read-only lookup against the fully-built
            // link graph. Safe in this multi-threaded render_task because no
            // writes occur (single-threaded population finished in Phase 1).
            if (cfg.wikilinks_enabled) {
                ctx["page"]["backlinks"] = link_graph.get_backlinks(rp.url);
            }

            std::string og_image_for_page = rp.parsed.frontmatter.image;
            if (og_image_for_page.empty()) {
                auto it = og_image_map.find(rp.url);
                if (it != og_image_map.end()) og_image_for_page = it->second;
            }
            // G9: tldr from frontmatter overrides description for meta description.
            std::string page_tldr;
            {
                auto it = rp.parsed.frontmatter.custom.find("tldr");
                if (it != rp.parsed.frontmatter.custom.end() && it->is_string()) {
                    page_tldr = it->get<std::string>();
                }
            }
            std::string seo_meta = build_seo_meta(
                rp.parsed.frontmatter.title, rp.url,
                rp.parsed.frontmatter.description,
                utils::truncate_text(utils::strip_html_tags(rp.html_content), 200),
                og_image_for_page, rp.parsed.frontmatter.canonical,
                cfg.site_base_url, cfg.site_twitter_handle, page_tldr);
            if (cfg.json_ld_enabled || cfg.citation_tags_enabled) {
                nlohmann::json schema_page;
                schema_page["title"]       = rp.parsed.frontmatter.title;
                schema_page["url"]         = rp.url;
                schema_page["date"]        = rp.parsed.frontmatter.date;
                schema_page["description"] = rp.parsed.frontmatter.description;
                schema_page["image"]       = og_image_for_page;
                schema_page["canonical"]   = rp.parsed.frontmatter.canonical;
                schema_page["excerpt"]     = utils::truncate_text(
                    utils::strip_html_tags(rp.html_content), 200);
                schema_page["tags"]        = tags;
                for (const auto& [k, v] : rp.parsed.frontmatter.custom.items()) {
                    schema_page[k] = v;
                }
                // Override the author slug string with a resolved Person
                // schema object (G6) so JSON-LD carries full identity data.
                if (!author_slug_resolved.empty()) {
                    schema_page["author"] = authors_index.person_schema(
                        author_slug_resolved,
                        cfg.site_base_url + authors_url_base +
                            author_slug_resolved + "/");
                }
                if (cfg.json_ld_enabled) {
                    seo_meta += modules::seo_schema::build_json_ld(cfg, schema_page, pages_array);
                }
                if (cfg.citation_tags_enabled) {
                    seo_meta += modules::seo_schema::build_citation_tags(cfg, schema_page);
                }
            }
            ctx["seo_meta"] = seo_meta;

            try {
                rendered_html[ti] = renderer.render(rp.parsed.frontmatter.layout,
                                                     ctx, rp.source_path);
                was_rendered[ti] = true;
            } catch (const RenderError& e) {
                std::lock_guard<std::mutex> lock(errors_mutex);
                result.errors.push_back({BuildError::Type::Template, e.source_file(),
                                         e.template_name(), e.line(), 0, e.what()});
            } catch (const std::runtime_error& e) {
                std::lock_guard<std::mutex> lock(errors_mutex);
                result.errors.push_back({BuildError::Type::Generic, rp.source_path,
                                         rp.parsed.frontmatter.layout, 0, 0, e.what()});
            }
        }
    };

    // Execute rendering
    if (thread_count > 1 && N > 1) {
        int actual_threads = std::min(thread_count, static_cast<int>(N));
        size_t chunk_size = (N + actual_threads - 1) / actual_threads;
        std::vector<std::thread> threads;
        for (int t = 0; t < actual_threads; t++) {
            size_t start = t * chunk_size;
            size_t end = std::min(start + chunk_size, N);
            if (start >= end) break;
            threads.emplace_back(render_task, start, end);
        }
        for (auto& th : threads) {
            th.join();
        }
    } else {
        render_task(0, N);
    }

    // Single-threaded collection of results
    size_t render_idx = 0;
    for (size_t i = 0; i < tasks.size(); i++) {
        auto& rp = raw_pages[i];
        std::vector<std::string> deps = {rp.source_path};
        if (!rp.template_path.empty()) {
            deps.push_back(rp.template_path);
        }
        for (const auto& ancestor : renderer.template_ancestors(rp.parsed.frontmatter.layout)) {
            deps.push_back(ancestor);
        }
        deps.insert(deps.end(), shortcode_deps.begin(), shortcode_deps.end());
        // Structural deps: invalidate every page when the site graph changes.
        deps.push_back("meta:pages_array");
        if (cfg.wikilinks_enabled) {
            deps.push_back("meta:wikilinks_index");
        }
        if (cfg.authors_enabled) {
            deps.push_back("meta:authors_index");
        }

        if (tasks[i].needs_rebuild) {
            if (was_rendered[render_idx]) {
                all_outputs.push_back({rp.output_path, rendered_html[render_idx]});
                result.pages_built++;
            }
            render_idx++;
        } else {
            result.pages_cached++;
        }

        PageRecord rec;
        rec.output_path = rp.output_path;
        rec.url = rp.url;
        rec.deps = deps;
        all_records.push_back(std::move(rec));
    }

    auto t_phase2 = std::chrono::high_resolution_clock::now();

    // --- Phase 3: Data-driven pages ---
    for (const auto& ds : cfg.data_sources) {
        std::string key = ds.file;
        auto dot_pos = key.rfind('.');
        if (dot_pos != std::string::npos) {
            key = key.substr(0, dot_pos);
        }

        if (!all_data.contains(key)) {
            std::cerr << utils::warning_label() << " data source references '"
                      << ds.file << "' but no matching data file found in "
                      << cfg.data_dir << "/\n";
            continue;
        }

        const nlohmann::json& items = all_data[key];
        if (!items.is_array()) {
            std::cerr << utils::warning_label() << " data source '"
                      << ds.file << "' is not an array — skipping\n";
            continue;
        }

        // Resolve data file path and template path for dependency tracking
        std::string data_file_path;
        for (const auto& df : data_files) {
            std::string stem = df;
            // Match by stem
            auto dp = df.rfind('/');
            std::string fname = (dp != std::string::npos) ? df.substr(dp + 1) : df;
            auto fe = fname.rfind('.');
            std::string fstem = (fe != std::string::npos) ? fname.substr(0, fe) : fname;
            if (fstem == key) {
                data_file_path = df;
                break;
            }
        }

        std::string tmpl_path = utils::path_join(cfg.template_dir, ds.template_name + ".html");

        // Check if data file or template changed (for all data-driven pages)
        bool data_changed = !incremental;
        if (incremental && !data_file_path.empty()) {
            data_changed = !hashes.is_unchanged(data_file_path);
        }
        bool tmpl_changed = !incremental;
        if (incremental && fs::exists(tmpl_path)) {
            tmpl_changed = !hashes.is_unchanged(tmpl_path);
        }
        bool rebuild_data_pages = !incremental || data_changed || tmpl_changed;

        if (rebuild_data_pages) {
            if (ds.per_page > 0) {
                build_paginated_pages(ds, items, site_ctx, pages_array,
                                      renderer, cfg.output_dir,
                                      data_file_path, tmpl_path,
                                      all_outputs, all_records,
                                      result.pages_built, &result.errors);
            }
            if (ds.per_item) {
                build_per_item_pages(cfg, ds, items, site_ctx, pages_array,
                                     renderer, cfg.output_dir,
                                     data_file_path, tmpl_path,
                                     all_outputs, all_records,
                                     result.pages_built, &result.errors);
            }
        } else {
            // Data-driven pages unchanged — count them as cached.
            // Register their output paths for orphan cleanup.
            if (ds.per_page > 0) {
                int total = static_cast<int>(items.size());
                int pp = ds.per_page > 0 ? ds.per_page : total;
                int npages = (total + pp - 1) / pp;
                result.pages_cached += npages;

                // Compute base URL for record registration
                std::string base_url;
                if (!ds.url_pattern.empty()) {
                    static const std::regex base_pattern(R"(([^{]*)\{\{)");
                    std::smatch match;
                    if (std::regex_search(ds.url_pattern, match, base_pattern)) {
                        base_url = match[1].str();
                    } else {
                        base_url = ds.url_pattern;
                    }
                } else {
                    base_url = "/" + ds.template_name + "s/";
                }
                if (!base_url.empty() && base_url.back() != '/') base_url += '/';

                for (int p = 0; p < npages; p++) {
                    std::string page_url = (p == 0) ? base_url
                        : base_url + "page/" + std::to_string(p + 1) + "/";
                    std::string output_path = utils::url_to_output(page_url, cfg.output_dir);

                    PageRecord rec;
                    rec.output_path = output_path;
                    rec.url = page_url;
                    rec.deps = {data_file_path, tmpl_path};
                    all_records.push_back(std::move(rec));
                }
            }
            if (ds.per_item) {
                result.pages_cached += static_cast<int>(items.size());

                for (const auto& item : items) {
                    std::string item_url;
                    if (!ds.url_pattern.empty()) {
                        item_url = interpolate_url(ds.url_pattern, item);
                    } else {
                        std::string slug;
                        if (item.contains(ds.item_key) && item[ds.item_key].is_string()) {
                            slug = item[ds.item_key].get<std::string>();
                        } else if (item.contains("slug") && item["slug"].is_string()) {
                            slug = item["slug"].get<std::string>();
                        } else if (item.contains("title") && item["title"].is_string()) {
                            slug = item["title"].get<std::string>();
                            std::transform(slug.begin(), slug.end(), slug.begin(),
                                [](unsigned char c) { return c == ' ' ? '-' : std::tolower(c); });
                        } else {
                            continue;
                        }
                        item_url = "/" + ds.template_name + "s/" + slug + "/";
                    }
                    if (!item_url.empty() && item_url.back() != '/') item_url += '/';
                    if (!item_url.empty() && item_url.front() != '/') item_url = '/' + item_url;

                    std::string output_path = utils::url_to_output(item_url, cfg.output_dir);

                    PageRecord rec;
                    rec.output_path = output_path;
                    rec.url = item_url;
                    rec.deps = {data_file_path, tmpl_path};
                    all_records.push_back(std::move(rec));
                }
            }
        }
    }

    // --- Add data-driven page URLs to pages_array for modules ---
    {
        std::unordered_set<std::string> known_urls;
        for (const auto& p : pages_array) {
            known_urls.insert(p.value("url", ""));
        }
        for (const auto& rec : all_records) {
            if (known_urls.count(rec.url)) continue;
            nlohmann::json page_meta;
            page_meta["title"] = "";
            page_meta["url"] = rec.url;
            page_meta["date"] = "";
            page_meta["description"]        = "";
            page_meta["image"]              = "";
            page_meta["canonical"]          = "";
            page_meta["sitemap_changefreq"] = "";
            page_meta["sitemap_priority"]   = "";
            pages_array.push_back(page_meta);
            known_urls.insert(rec.url);
        }
    }

    // --- Phase 3.5: Generate alias redirect pages ---
    // Aliases must NOT appear in {{ pages }} during rendering (Phase 2 is done
    // by this point) but MUST appear in sitemap. We add them to pages_array now.
    for (const auto& ae : alias_entries) {
        // Normalize alias URL → on-disk output path.
        std::string rel = ae.alias_url;
        while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());

        std::string alias_output;
        if (rel.empty()) {
            alias_output = cfg.output_dir + "/index.html";
        } else {
            auto last_slash = rel.rfind('/');
            std::string last_seg = (last_slash != std::string::npos)
                ? rel.substr(last_slash + 1) : rel;
            bool has_ext = !last_seg.empty() && last_seg.find('.') != std::string::npos;
            if (has_ext) {
                alias_output = cfg.output_dir + "/" + rel;
            } else if (!rel.empty() && rel.back() == '/') {
                alias_output = cfg.output_dir + "/" + rel + "index.html";
            } else {
                alias_output = cfg.output_dir + "/" + rel + "/index.html";
            }
        }

        // Build the redirect HTML.
        std::string html = "<!DOCTYPE html>\n<html>\n<head>\n"
            "<meta charset=\"utf-8\">\n"
            "<title>Redirecting...</title>\n"
            "<link rel=\"canonical\" href=\"" + ae.target_url + "\">\n"
            "<meta http-equiv=\"refresh\" content=\"0; url=" + ae.target_url + "\">\n"
            "</head>\n<body>\n"
            "<p>Redirecting to <a href=\"" + ae.target_url + "\">" +
            ae.target_url + "</a>.</p>\n"
            "</body>\n</html>";

        all_outputs.push_back({alias_output, html});

        PageRecord rec;
        rec.output_path = alias_output;
        rec.url = ae.alias_url;
        all_records.push_back(std::move(rec));

        nlohmann::json alias_meta;
        alias_meta["title"] = "";
        alias_meta["url"] = ae.alias_url;
        alias_meta["date"] = ae.date;
        pages_array.push_back(alias_meta);

        result.pages_built++;
    }

    // --- Write outputs ---
    // Only clean output dir and rewrite on full rebuild or if there are changes.
    // For incremental: we need to be smarter — only remove orphaned outputs.

    // Apply HTML minification to all rendered outputs if enabled.
    if (cfg.minify_html) {
        for (auto& out : all_outputs) {
            out.html = minify_html(out.html);
        }
    }
    if (!incremental) {
        // Full rebuild: clean and write everything
        if (fs::exists(cfg.output_dir)) {
            fs::remove_all(cfg.output_dir);
        }
        fs::create_directories(cfg.output_dir);

        for (const auto& out : all_outputs) {
            utils::write_file(out.output_path, out.html);
        }
    } else {
        // Incremental: only write new/changed outputs
        for (const auto& out : all_outputs) {
            utils::write_file(out.output_path, out.html);
        }

        // Remove orphaned HTML outputs (pages whose source was deleted).
        // Only remove .html files here — the asset pipeline handles its own orphans.
        std::unordered_set<std::string> active_outputs;
        for (const auto& rec : all_records) {
            active_outputs.insert(rec.output_path);
        }

        if (fs::exists(cfg.output_dir)) {
            for (const auto& entry : fs::recursive_directory_iterator(cfg.output_dir)) {
                if (!entry.is_regular_file()) continue;
                std::string path = entry.path().string();
                // Only clean up HTML files — assets are managed separately
                if (path.size() < 5 || path.substr(path.size() - 5) != ".html") continue;
                if (active_outputs.find(path) == active_outputs.end()) {
                    fs::remove(path);
                    result.pages_removed++;
                }
            }
        }
    }

    auto t_phase3 = std::chrono::high_resolution_clock::now();

    // --- Process static assets (copy + minify + optional image optimization + fingerprinting) ---
    {
        std::vector<std::string> asset_paths;
        AssetManifest* manifest_ptr = cfg.fingerprint_assets ? &asset_manifest : nullptr;
        auto asset_result = process_assets(cfg, hashes, incremental, asset_paths, manifest_ptr);
        result.assets_copied = asset_result.files_copied;
        result.assets_minified = asset_result.files_minified;
        result.assets_cached = asset_result.files_cached;
        result.assets_removed = asset_result.files_removed;
        result.bytes_saved = asset_result.bytes_saved;
    }

    auto t_assets = std::chrono::high_resolution_clock::now();

    // --- Generate OG image files (after the output wipe + asset pipeline) ---
    if (cfg.og_images_enabled) {
        modules::generate_og_images(cfg, pages_array, cfg.output_dir, cfg.template_dir);
    }

    // --- Generate built-in modules (sitemap, RSS, robots, 404) ---
    // NOTE: Runs AFTER asset pipeline so module files are always fresh and
    // not subject to orphan cleanup.
    {
        if (cfg.module_sitemap) {
            modules::generate_sitemap(cfg, pages_array, cfg.output_dir);
        }
        if (cfg.module_rss) {
            modules::generate_rss(cfg, pages_array, cfg.output_dir);
        }
        if (cfg.module_json_feed) {
            modules::generate_json_feed(cfg, pages_array, cfg.output_dir);
        }
        if (cfg.module_llms_txt) {
            modules::generate_llms_txt(cfg, pages_array, cfg.output_dir);
        }
        if (cfg.module_robots) {
            modules::generate_robots(cfg, cfg.output_dir);
        }
        if (cfg.search_enabled) {
            modules::generate_search_index(cfg, pages_array, cfg.output_dir);
        }
        // Generate built-in 404 page if no src/404.md was processed
        bool has_404 = false;
        for (const auto& rp : raw_pages) {
            if (rp.url == "/404.html") { has_404 = true; break; }
        }
        if (!has_404) {
            std::string html_404 = generate_builtin_404(cfg);
            utils::write_file(cfg.output_dir + "/404.html", html_404);
        }
        // Generate syntax highlighting stylesheet when enabled.
        if (cfg.highlight_enabled) {
            std::string css_dir = cfg.output_dir + "/css";
            fs::create_directories(css_dir);
            utils::write_file(css_dir + "/highlight.css",
                              highlight_css(cfg.highlight_style));
        }
    }

    auto t_modules = std::chrono::high_resolution_clock::now();

    // --- Run after_build hook ---
    if (!run_hook(cfg.hook_after_build, cfg.env, cfg.output_dir, result.pages_built)) {
        result.errors.push_back({
            BuildError::Type::Generic, "", "after_build", 0, 0,
            "after_build hook failed: '" + cfg.hook_after_build + "'"
        });
    }

    // --- Save hash cache ---
    // Build the list of all active dependency keys for pruning
    std::vector<std::string> all_deps;
    for (const auto& [key, _] : hashes.current_hashes()) {
        all_deps.push_back(key);
    }
    hashes.prune_deleted(all_deps);
    hashes.save();

    auto end = std::chrono::high_resolution_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
    result.phase1_ms  = std::chrono::duration<double, std::milli>(t_phase1 - start).count();
    result.phase2_ms  = std::chrono::duration<double, std::milli>(t_phase2 - t_phase1).count();
    result.phase3_ms  = std::chrono::duration<double, std::milli>(t_phase3 - t_phase2).count();
    result.asset_ms   = std::chrono::duration<double, std::milli>(t_assets - t_phase3).count();
    result.module_ms  = std::chrono::duration<double, std::milli>(t_modules - t_assets).count();

    return result;
}

} // namespace cstatic
