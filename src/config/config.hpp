#pragma once

#include <string>
#include <stdexcept>

namespace cstatic {

// Thrown when config.toml is missing, unreadable, or semantically invalid.
class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Pure-data configuration struct — one field per config.toml key.
struct Config {
    // [site]
    std::string site_title;
    std::string site_base_url;
    std::string site_language = "en";

    // [build]
    std::string source_dir    = "src";
    std::string output_dir    = "output";
    std::string template_dir  = "templates";
    std::string static_dir    = "static";

    // [build.incremental]
    bool        incremental_enabled = true;
    std::string incremental_hash_file = ".cstatic_cache/hashes.json";

    // [build.minify]
    bool minify_css = true;
    bool minify_js  = true;

    // [modules]
    bool module_sitemap = true;
    bool module_rss     = false;
    bool module_robots  = false;

    // [modules.rss] (used only when module_rss = true)
    int         rss_item_count = 20;
    std::string rss_title;
    std::string rss_description;

    // [modules.robots]
    std::string robots_user_agent = "*";
    bool        robots_include_sitemap = true;
    std::vector<std::string> robots_disallow;

    // [sitemap]
    std::vector<std::string> sitemap_exclude;

    // [data]
    std::string data_dir = "_data";

    // [[data_source]] — array of tables for data-driven page generation
    struct DataSource {
        std::string file;           // e.g. "products.json"
        std::string template_name;  // e.g. "product"
        std::string url_pattern;    // e.g. "/products/{{ slug }}/"
        std::string item_key;       // e.g. "slug" — field used for URL interpolation
        int per_page = 0;           // 0 = no pagination for this source
        bool per_item = false;      // generate individual pages per item
    };
    std::vector<DataSource> data_sources;
};

// Load and validate config from the given TOML file path.
// Throws ConfigError on any problem (file missing, bad TOML, invalid values).
Config load_config(const std::string& path);

// Serialize config to JSON string (for cache/debugging).
std::string config_to_json(const Config& cfg);

} // namespace cstatic
