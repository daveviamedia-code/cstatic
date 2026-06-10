#include "config/config.hpp"
#include "utils/terminal.hpp"

#include <toml++/toml.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>

namespace cstatic {

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

} // anonymous namespace

Config load_config(const std::string& path) {
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

    Config cfg;

    // --- [site] (required section) ---
    cfg.site_title    = require_string(tbl, "site.title");
    cfg.site_base_url = require_string(tbl, "site.base_url");
    cfg.site_language  = optional_string(tbl, "site.language", cfg.site_language);

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
    cfg.minify_css = optional_bool(tbl, "build.minify.css", cfg.minify_css);
    cfg.minify_js  = optional_bool(tbl, "build.minify.js",  cfg.minify_js);

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

    return cfg;
}

std::string config_to_json(const Config& cfg) {
    nlohmann::json j;
    j["site"]["title"]    = cfg.site_title;
    j["site"]["base_url"] = cfg.site_base_url;
    j["site"]["language"] = cfg.site_language;

    j["build"]["source_dir"]   = cfg.source_dir;
    j["build"]["output_dir"]   = cfg.output_dir;
    j["build"]["template_dir"] = cfg.template_dir;
    j["build"]["static_dir"]   = cfg.static_dir;

    j["build"]["incremental"]["enabled"]    = cfg.incremental_enabled;
    j["build"]["incremental"]["hash_file"]  = cfg.incremental_hash_file;

    j["build"]["minify"]["css"] = cfg.minify_css;
    j["build"]["minify"]["js"]  = cfg.minify_js;

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

    return j.dump(2);
}

} // namespace cstatic
