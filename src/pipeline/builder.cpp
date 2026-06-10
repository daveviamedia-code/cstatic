#include "pipeline/builder.hpp"
#include "assets/asset_pipeline.hpp"
#include "config/config.hpp"
#include "content/frontmatter.hpp"
#include "content/markdown.hpp"
#include "data/data_loader.hpp"
#include "hash/hash_store.hpp"
#include "modules/sitemap.hpp"
#include "modules/rss.hpp"
#include "modules/robots.hpp"
#include "template/renderer.hpp"
#include "utils/path.hpp"
#include "utils/terminal.hpp"

#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <regex>
#include <unordered_set>

namespace cstatic {

namespace fs = std::filesystem;

// --- File I/O helpers ---

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error(
            std::string(utils::error_label()) + " cannot read file: " + path +
            " — file does not exist or is unreadable");
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void write_file(const std::string& path, const std::string& content) {
    std::string dir = utils::parent_dir(path);
    if (!dir.empty() && !fs::exists(dir)) {
        fs::create_directories(dir);
    }
    std::ofstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error(
            std::string(utils::error_label()) + " cannot write output file: " + path +
            " — check that the output directory is writable");
    }
    f << content;
}

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
    int& pages_built
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

        std::string html = renderer.render(ds.template_name, ctx);
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
    int& pages_built
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

        std::string html = renderer.render(ds.template_name, ctx);
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

BuildResult build_site(const Config& cfg, bool full_rebuild) {
    auto start = std::chrono::high_resolution_clock::now();
    BuildResult result;

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
        std::string config_contents = read_file("config.toml");
        hashes.hash_string(config_key, config_contents);
    }

    // If config changed and incremental, force full rebuild
    if (incremental && !hashes.is_unchanged_key(config_key)) {
        incremental = false;
        // Re-initialize without previous hashes to force clean rebuild
        hashes = HashStore(cfg.incremental_hash_file);
        hashes.hash_string(config_key, read_file("config.toml"));
    }

    // Source markdown files
    auto md_files = collect_files(cfg.source_dir, ".md");
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

    // --- Load data ---
    DataLoader data_loader(cfg.data_dir);
    nlohmann::json all_data = data_loader.load_all();

    // --- Set up renderer and site context ---
    TemplateRenderer renderer(cfg.template_dir);

    nlohmann::json site_ctx;
    site_ctx["title"] = cfg.site_title;
    site_ctx["base_url"] = cfg.site_base_url;
    site_ctx["language"] = cfg.site_language;

    // --- Phase 1: Parse all markdown pages ---
    struct RawPage {
        std::string source_path;
        std::string url;
        std::string output_path;
        ParsedContent parsed;
        std::string html_content;
        std::string template_path; // resolved template file path
    };

    std::vector<RawPage> raw_pages;
    nlohmann::json pages_array = nlohmann::json::array();

    for (const auto& file_path : md_files) {
        RawPage rp;
        rp.source_path = file_path;

        std::string content = read_file(file_path);
        rp.parsed = parse_frontmatter(content, file_path);

        if (rp.parsed.frontmatter.draft) {
            result.pages_skipped++;
            continue;
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
        rp.html_content = render_markdown(rp.parsed.body);

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

        nlohmann::json tags = nlohmann::json::array();
        for (const auto& tag : rp.parsed.frontmatter.tags) {
            tags.push_back(tag);
        }

        nlohmann::json page_meta;
        page_meta["title"] = title;
        page_meta["url"] = rp.url;
        page_meta["date"] = rp.parsed.frontmatter.date;
        page_meta["tags"] = tags;
        pages_array.push_back(page_meta);

        rp.parsed.frontmatter.title = title;
        raw_pages.push_back(std::move(rp));
    }

    std::sort(pages_array.begin(), pages_array.end(),
        [](const nlohmann::json& a, const nlohmann::json& b) {
            return a.value("date", "") > b.value("date", "");
        });

    // --- Phase 2: Render markdown pages, track dependencies, decide what to rebuild ---
    std::vector<CachedOutput> all_outputs;
    std::vector<PageRecord> all_records;

    for (auto& rp : raw_pages) {
        // Build dependency list for this page
        std::vector<std::string> deps = {rp.source_path};
        if (!rp.template_path.empty()) {
            deps.push_back(rp.template_path);
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
        }

        if (needs_rebuild) {
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
            for (const auto& [key, val] : rp.parsed.frontmatter.custom) {
                ctx["page"][key] = val;
            }

            ctx["site"] = site_ctx;
            ctx["pages"] = pages_array;
            ctx["data"] = all_data;

            std::string html = renderer.render(rp.parsed.frontmatter.layout, ctx);
            all_outputs.push_back({rp.output_path, html});
            result.pages_built++;
        } else {
            result.pages_cached++;
        }

        PageRecord rec;
        rec.output_path = rp.output_path;
        rec.url = rp.url;
        rec.deps = deps;
        all_records.push_back(std::move(rec));
    }

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
                                      result.pages_built);
            }
            if (ds.per_item) {
                build_per_item_pages(ds, items, site_ctx, pages_array,
                                     renderer, cfg.output_dir,
                                     data_file_path, tmpl_path,
                                     all_outputs, all_records,
                                     result.pages_built);
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
            pages_array.push_back(page_meta);
            known_urls.insert(rec.url);
        }
    }

    // --- Write outputs ---
    // Only clean output dir and rewrite on full rebuild or if there are changes.
    // For incremental: we need to be smarter — only remove orphaned outputs.
    if (!incremental) {
        // Full rebuild: clean and write everything
        if (fs::exists(cfg.output_dir)) {
            fs::remove_all(cfg.output_dir);
        }
        fs::create_directories(cfg.output_dir);

        for (const auto& out : all_outputs) {
            write_file(out.output_path, out.html);
        }
    } else {
        // Incremental: only write new/changed outputs
        for (const auto& out : all_outputs) {
            write_file(out.output_path, out.html);
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

    // --- Process static assets (copy + minify) ---
    {
        std::vector<std::string> asset_paths;
        auto asset_result = process_assets(cfg, hashes, incremental, asset_paths);
        result.assets_copied = asset_result.files_copied;
        result.assets_minified = asset_result.files_minified;
        result.assets_cached = asset_result.files_cached;
        result.assets_removed = asset_result.files_removed;
        result.bytes_saved = asset_result.bytes_saved;
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
        if (cfg.module_robots) {
            modules::generate_robots(cfg, cfg.output_dir);
        }
        // Generate built-in 404 page if no src/404.md was processed
        bool has_404 = false;
        for (const auto& rp : raw_pages) {
            if (rp.url == "/404.html") { has_404 = true; break; }
        }
        if (!has_404) {
            std::string html_404 = generate_builtin_404(cfg);
            write_file(cfg.output_dir + "/404.html", html_404);
        }
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

    return result;
}

} // namespace cstatic
