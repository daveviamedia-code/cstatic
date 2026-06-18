#include "config/config.hpp"
#include "utils/terminal.hpp"

#include <toml++/toml.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace cstatic {

namespace fs = std::filesystem;

namespace {

// Read a file into a string. Throws ConfigError if unreadable.
std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw ConfigError(
            utils::error_label() + " cannot open config file: " + path
        );
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Build a type-mismatch error message for a config key.
// Uses the underlying node for source line info.
std::string type_error(const std::string& dotted_key, const std::string& expected,
                       const toml::node_view<const toml::node>& nv) {
    std::ostringstream msg;
    msg << utils::error_label() << " config.toml: key '" << dotted_key
        << "' must be " << expected << ", got " << nv.type();
    if (nv.node()) {
        auto src = nv.node()->source();
        if (src.begin.line) {
            msg << " (line " << src.begin.line << ")";
        }
    }
    return msg.str();
}

// Require a string value at the given dotted key (e.g. "site.title").
// Returns the value. Throws ConfigError if missing or wrong type.
std::string require_string(const toml::table& tbl, const std::string& dotted_key) {
    auto nv = toml::at_path(tbl, dotted_key);
    if (!nv) {
        throw ConfigError(
            utils::error_label() + " config.toml: missing required key '" +
            dotted_key + "'"
        );
    }
    auto val = nv.as_string();
    if (!val) {
        throw ConfigError(type_error(dotted_key, "a string", nv));
    }
    return std::string(val->get());
}

// Optional string — returns default if missing, errors if wrong type.
std::string optional_string(const toml::table& tbl, const std::string& dotted_key,
                            const std::string& default_val) {
    auto nv = toml::at_path(tbl, dotted_key);
    if (!nv) return default_val;
    auto val = nv.as_string();
    if (!val) {
        throw ConfigError(type_error(dotted_key, "a string", nv));
    }
    return std::string(val->get());
}

// Optional bool — returns default if missing, errors if wrong type.
bool optional_bool(const toml::table& tbl, const std::string& dotted_key,
                   bool default_val) {
    auto nv = toml::at_path(tbl, dotted_key);
    if (!nv) return default_val;
    auto val = nv.as_boolean();
    if (!val) {
        throw ConfigError(type_error(dotted_key, "a boolean", nv));
    }
    return val->get();
}

// Optional int — returns default if missing, errors if wrong type.
int optional_int(const toml::table& tbl, const std::string& dotted_key,
                 int default_val) {
    auto nv = toml::at_path(tbl, dotted_key);
    if (!nv) return default_val;
    auto val = nv.as_integer();
    if (!val) {
        throw ConfigError(type_error(dotted_key, "an integer", nv));
    }
    return static_cast<int>(val->get());
}

// Optional string array — returns empty if missing.
std::vector<std::string> optional_string_array(const toml::table& tbl,
                                                const std::string& dotted_key) {
    auto nv = toml::at_path(tbl, dotted_key);
    if (!nv) return {};
    auto arr = nv.as_array();
    if (!arr) {
        throw ConfigError(type_error(dotted_key, "an array", nv));
    }
    std::vector<std::string> result;
    for (auto& elem : *arr) {
        auto s = elem.as_string();
        if (!s) {
            std::ostringstream msg;
            msg << utils::error_label() << " config.toml: elements of '"
                << dotted_key << "' must be strings";
            auto src = elem.source();
            if (src.begin.line) {
                msg << " (line " << src.begin.line << ")";
            }
            throw ConfigError(msg.str());
        }
        result.push_back(std::string(s->get()));
    }
    return result;
}

// Deep-merge overlay table into base table. Tables recurse; everything else
// (including arrays) is replaced wholesale.
void merge_table(toml::table& base, const toml::table& overlay) {
    for (const auto& [k, v] : overlay) {
        auto* bn = base.get(k);
        if (bn && bn->is_table() && v.is_table()) {
            merge_table(*bn->as_table(), *v.as_table());
        } else if (v.is_table()) {
            base.insert_or_assign(k, toml::table(*v.as_table()));
        } else if (v.is_array()) {
            base.insert_or_assign(k, toml::array(*v.as_array()));
        } else if (v.is_string()) {
            base.insert_or_assign(k, v.as_string()->get());
        } else if (v.is_integer()) {
            base.insert_or_assign(k, v.as_integer()->get());
        } else if (v.is_floating_point()) {
            base.insert_or_assign(k, v.as_floating_point()->get());
        } else if (v.is_boolean()) {
            base.insert_or_assign(k, v.as_boolean()->get());
        }
    }
}

} // anonymous namespace

Config load_config(const std::string& path, const std::string& env) {
    std::string contents = read_file(path);

    toml::table tbl;
    try {
        tbl = toml::parse(contents, path);
    } catch (const toml::parse_error& err) {
        std::ostringstream msg;
        msg << utils::error_label() << " config.toml:" << err.source().begin.line
            << " — " << err.description();
        throw ConfigError(msg.str());
    }

    // Apply environment overlay if requested.
    if (!env.empty()) {
        fs::path base(path);
        std::string overlay_path = (base.parent_path() /
            (base.stem().string() + "." + env + base.extension().string())).string();

        if (fs::exists(overlay_path)) {
            std::string overlay_contents = read_file(overlay_path);
            try {
                toml::table overlay_tbl = toml::parse(overlay_contents, overlay_path);
                merge_table(tbl, overlay_tbl);
            } catch (const toml::parse_error& err) {
                std::ostringstream msg;
                msg << utils::error_label() << " " << overlay_path << ":"
                    << err.source().begin.line << " — " << err.description();
                throw ConfigError(msg.str());
            }
        } else {
            std::cerr << utils::warning_label() << " environment config '"
                      << overlay_path << "' not found — using base config only\n";
        }
    }

    Config cfg;
    cfg.env = env.empty() ? "development" : env;

    // --- [site] (required section) ---
    cfg.site_title    = require_string(tbl, "site.title");
    cfg.site_base_url = require_string(tbl, "site.base_url");
    cfg.site_language  = optional_string(tbl, "site.language", cfg.site_language);
    cfg.site_twitter_handle = optional_string(tbl, "site.twitter_handle", cfg.site_twitter_handle);

    // Validate base_url looks like a URL
    if (cfg.site_base_url.find("://") == std::string::npos) {
        throw ConfigError(
            utils::error_label() + " config.toml: 'site.base_url' must be a "
            "full URL (e.g. \"https://example.com\"), got \"" +
            cfg.site_base_url + "\""
        );
    }
    // Strip trailing slash for consistent URL joining later
    if (!cfg.site_base_url.empty() && cfg.site_base_url.back() == '/') {
        cfg.site_base_url.pop_back();
    }

    // --- [build] ---
    cfg.source_dir   = optional_string(tbl, "build.source_dir",   cfg.source_dir);
    cfg.output_dir   = optional_string(tbl, "build.output_dir",   cfg.output_dir);
    cfg.template_dir = optional_string(tbl, "build.template_dir", cfg.template_dir);
    cfg.static_dir   = optional_string(tbl, "build.static_dir",   cfg.static_dir);

    // --- [build.incremental] ---
    cfg.incremental_enabled   = optional_bool(tbl, "build.incremental.enabled",   cfg.incremental_enabled);
    cfg.incremental_hash_file = optional_string(tbl, "build.incremental.hash_file", cfg.incremental_hash_file);

    // --- [build.minify] ---
    cfg.minify_css  = optional_bool(tbl, "build.minify.css", cfg.minify_css);
    cfg.minify_js   = optional_bool(tbl, "build.minify.js",  cfg.minify_js);
    cfg.minify_html = optional_bool(tbl, "build.minify.html", cfg.minify_html);

    // --- [build.images] ---
    cfg.images_optimize  = optional_bool(tbl, "build.images.optimize",  cfg.images_optimize);
    cfg.images_max_width = optional_int(tbl,  "build.images.max_width", cfg.images_max_width);
    cfg.images_quality   = optional_int(tbl,  "build.images.quality",   cfg.images_quality);
    cfg.images_webp      = optional_bool(tbl, "build.images.webp",      cfg.images_webp);
    cfg.images_avif      = optional_bool(tbl, "build.images.avif",      cfg.images_avif);

    // --- [build] fingerprint ---
    cfg.fingerprint_assets = optional_bool(tbl, "build.fingerprint_assets", cfg.fingerprint_assets);

    // --- [build] scheduled publishing ---
    cfg.publish_future = optional_bool(tbl, "build.publish_future", cfg.publish_future);

    // --- [build.search] ---
    cfg.search_enabled = optional_bool(tbl,   "build.search.enabled", cfg.search_enabled);
    cfg.search_output  = optional_string(tbl, "build.search.output",  cfg.search_output);

    // --- [build.highlight] ---
    cfg.highlight_enabled = optional_bool(tbl,   "build.highlight.enabled", cfg.highlight_enabled);
    cfg.highlight_style   = optional_string(tbl, "build.highlight.style",   cfg.highlight_style);

    // --- [build.markdown] ---
    cfg.markdown_extensions = optional_string_array(tbl, "build.markdown.extensions");
    cfg.shortcodes_dir      = optional_string(tbl,      "build.markdown.shortcodes_dir", cfg.shortcodes_dir);
    cfg.wikilinks_enabled   = optional_bool(tbl,        "build.markdown.wikilinks",       cfg.wikilinks_enabled);

    // --- [og_images] ---
    cfg.og_images_enabled       = optional_bool(tbl,   "og_images.enabled",       cfg.og_images_enabled);
    cfg.og_images_template      = optional_string(tbl, "og_images.template",      cfg.og_images_template);
    cfg.og_images_output_format = optional_string(tbl, "og_images.output_format", cfg.og_images_output_format);
    cfg.og_images_width         = optional_int(tbl,    "og_images.width",         cfg.og_images_width);
    cfg.og_images_height        = optional_int(tbl,    "og_images.height",        cfg.og_images_height);
    cfg.og_images_output_dir    = optional_string(tbl, "og_images.output_dir",    cfg.og_images_output_dir);

    // --- [modules] ---
    cfg.module_sitemap = optional_bool(tbl, "modules.sitemap", cfg.module_sitemap);
    cfg.module_rss     = optional_bool(tbl, "modules.rss",     cfg.module_rss);
    cfg.module_robots  = optional_bool(tbl, "modules.robots",  cfg.module_robots);

    // --- [modules.rss] ---
    cfg.rss_title       = optional_string(tbl, "modules.rss_title", cfg.site_title);
    cfg.rss_description = optional_string(tbl, "modules.rss_description", "");
    cfg.rss_item_count  = optional_int(tbl, "modules.rss_item_count", cfg.rss_item_count);

    // --- [modules.robots] ---
    cfg.robots_user_agent      = optional_string(tbl, "modules.robots_user_agent", cfg.robots_user_agent);
    cfg.robots_include_sitemap = optional_bool(tbl, "modules.robots_include_sitemap", cfg.robots_include_sitemap);
    cfg.robots_disallow        = optional_string_array(tbl, "modules.robots_disallow");

    // --- [sitemap] ---
    cfg.sitemap_exclude = optional_string_array(tbl, "sitemap.exclude");

    // --- [hooks] ---
    cfg.hook_before_build = optional_string(tbl, "hooks.before_build", "");
    cfg.hook_after_build  = optional_string(tbl, "hooks.after_build", "");

    // --- [check] ---
    cfg.check_external   = optional_bool(tbl, "check.external",    cfg.check_external);
    cfg.check_timeout_ms = optional_int (tbl, "check.timeout_ms", cfg.check_timeout_ms);

    // --- [data] ---
    cfg.data_dir = optional_string(tbl, "data.data_dir", cfg.data_dir);

    // --- [[data_source]] ---
    auto ds_arr = toml::at_path(tbl, "data_source");
    if (ds_arr.is_array_of_tables()) {
        for (auto& elem : *ds_arr.as_array()) {
            auto& ds_tbl = *elem.as_table();
            Config::DataSource ds;
            // Use a temporary table reference for at_path lookups
            ds.file          = require_string(ds_tbl, "file");
            ds.template_name = require_string(ds_tbl, "template");
            ds.url_pattern   = optional_string(ds_tbl, "url_pattern", "");
            ds.item_key      = optional_string(ds_tbl, "item_key", "slug");
            ds.per_page      = optional_int(ds_tbl, "per_page", 0);
            ds.per_item      = optional_bool(ds_tbl, "per_item", false);
            cfg.data_sources.push_back(std::move(ds));
        }
    }

    // --- [[pagination]] ---
    auto pg_arr = toml::at_path(tbl, "pagination");
    if (pg_arr.is_array_of_tables()) {
        for (auto& elem : *pg_arr.as_array()) {
            auto& pg_tbl = *elem.as_table();
            Config::PaginationRule pr;
            pr.source     = require_string(pg_tbl, "source");
            pr.template_  = require_string(pg_tbl, "template");
            pr.per_page   = optional_int(pg_tbl, "per_page", 10);
            cfg.pagination_rules.push_back(std::move(pr));
        }
    }

    // --- [[collection]] ---
    auto col_arr = toml::at_path(tbl, "collection");
    if (col_arr.is_array_of_tables()) {
        for (auto& elem : *col_arr.as_array()) {
            auto& col_tbl = *elem.as_table();
            Config::Collection col;
            col.name           = require_string(col_tbl, "name");
            col.template_      = require_string(col_tbl, "template");
            col.index_template = optional_string(col_tbl, "index_template", col.name + "-index");
            col.url_pattern    = optional_string(col_tbl, "url_pattern", "");
            col.sort_by        = optional_string(col_tbl, "sort_by", "date");
            col.sort_order     = optional_string(col_tbl, "sort_order", "desc");
            cfg.collections.push_back(std::move(col));
        }
    }

    // --- [[taxonomy]] ---
    auto tax_arr = toml::at_path(tbl, "taxonomy");
    if (tax_arr.is_array_of_tables()) {
        for (auto& elem : *tax_arr.as_array()) {
            auto& tax_tbl = *elem.as_table();
            Config::Taxonomy tax;
            tax.key            = require_string(tax_tbl, "key");
            tax.template_      = require_string(tax_tbl, "template");
            tax.index_template = optional_string(tax_tbl, "index_template", tax.key);
            cfg.taxonomies.push_back(std::move(tax));
        }
    }

    return cfg;
}

std::string config_to_json(const Config& cfg) {
    nlohmann::json j;
    j["site"]["title"]    = cfg.site_title;
    j["site"]["base_url"] = cfg.site_base_url;
    j["site"]["language"] = cfg.site_language;
    j["site"]["twitter_handle"] = cfg.site_twitter_handle;
    j["site"]["env"] = cfg.env;

    j["build"]["source_dir"]   = cfg.source_dir;
    j["build"]["output_dir"]   = cfg.output_dir;
    j["build"]["template_dir"] = cfg.template_dir;
    j["build"]["static_dir"]   = cfg.static_dir;

    j["build"]["incremental"]["enabled"]    = cfg.incremental_enabled;
    j["build"]["incremental"]["hash_file"]  = cfg.incremental_hash_file;

    j["build"]["minify"]["css"]  = cfg.minify_css;
    j["build"]["minify"]["js"]   = cfg.minify_js;
    j["build"]["minify"]["html"] = cfg.minify_html;

    j["build"]["images"]["optimize"]  = cfg.images_optimize;
    j["build"]["images"]["max_width"] = cfg.images_max_width;
    j["build"]["images"]["quality"]   = cfg.images_quality;
    j["build"]["images"]["webp"]      = cfg.images_webp;
    j["build"]["images"]["avif"]      = cfg.images_avif;

    j["build"]["fingerprint_assets"] = cfg.fingerprint_assets;
    j["build"]["publish_future"]     = cfg.publish_future;

    j["build"]["search"]["enabled"] = cfg.search_enabled;
    j["build"]["search"]["output"]  = cfg.search_output;

    j["build"]["highlight"]["enabled"] = cfg.highlight_enabled;
    j["build"]["highlight"]["style"]   = cfg.highlight_style;

    j["build"]["markdown"]["extensions"]     = cfg.markdown_extensions;
    j["build"]["markdown"]["shortcodes_dir"] = cfg.shortcodes_dir;
    j["build"]["markdown"]["wikilinks"]      = cfg.wikilinks_enabled;

    j["og_images"]["enabled"]       = cfg.og_images_enabled;
    j["og_images"]["template"]      = cfg.og_images_template;
    j["og_images"]["output_format"] = cfg.og_images_output_format;
    j["og_images"]["width"]         = cfg.og_images_width;
    j["og_images"]["height"]        = cfg.og_images_height;
    j["og_images"]["output_dir"]    = cfg.og_images_output_dir;

    j["modules"]["sitemap"] = cfg.module_sitemap;
    j["modules"]["rss"]     = cfg.module_rss;
    j["modules"]["robots"]  = cfg.module_robots;

    j["modules"]["rss_title"]       = cfg.rss_title;
    j["modules"]["rss_description"] = cfg.rss_description;
    j["modules"]["rss_item_count"]  = cfg.rss_item_count;

    j["modules"]["robots_user_agent"]       = cfg.robots_user_agent;
    j["modules"]["robots_include_sitemap"]  = cfg.robots_include_sitemap;
    j["modules"]["robots_disallow"]         = cfg.robots_disallow;

    j["sitemap"]["exclude"] = cfg.sitemap_exclude;

    j["hooks"]["before_build"] = cfg.hook_before_build;
    j["hooks"]["after_build"]  = cfg.hook_after_build;

    j["check"]["external"]    = cfg.check_external;
    j["check"]["timeout_ms"]  = cfg.check_timeout_ms;

    j["data"]["data_dir"] = cfg.data_dir;

    auto sources = nlohmann::json::array();
    for (const auto& ds : cfg.data_sources) {
        nlohmann::json s;
        s["file"]          = ds.file;
        s["template"]      = ds.template_name;
        s["url_pattern"]   = ds.url_pattern;
        s["item_key"]      = ds.item_key;
        s["per_page"]      = ds.per_page;
        s["per_item"]      = ds.per_item;
        sources.push_back(s);
    }
    j["data_sources"] = sources;

    auto rules = nlohmann::json::array();
    for (const auto& pr : cfg.pagination_rules) {
        nlohmann::json r;
        r["source"]    = pr.source;
        r["template"]  = pr.template_;
        r["per_page"]  = pr.per_page;
        rules.push_back(r);
    }
    j["pagination"] = rules;

    auto cols = nlohmann::json::array();
    for (const auto& col : cfg.collections) {
        nlohmann::json c;
        c["name"]           = col.name;
        c["template"]       = col.template_;
        c["index_template"] = col.index_template;
        c["url_pattern"]    = col.url_pattern;
        c["sort_by"]        = col.sort_by;
        c["sort_order"]     = col.sort_order;
        cols.push_back(c);
    }
    j["collections"] = cols;

    auto taxes = nlohmann::json::array();
    for (const auto& tax : cfg.taxonomies) {
        nlohmann::json t;
        t["key"]            = tax.key;
        t["template"]       = tax.template_;
        t["index_template"] = tax.index_template;
        taxes.push_back(t);
    }
    j["taxonomies"] = taxes;

    return j.dump(2);
}

} // namespace cstatic
