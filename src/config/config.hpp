#pragma once

#include <string>
#include <stdexcept>
#include <vector>

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
    std::string site_twitter_handle;        // e.g. "@username"
    std::string site_description;           // site summary; llms.txt fallback
    std::string env = "development";

    // [build]
    std::string source_dir    = "src";
    std::string output_dir    = "output";
    std::string template_dir  = "templates";
    std::string static_dir    = "static";

    // [build.incremental]
    bool        incremental_enabled = true;
    std::string incremental_hash_file = ".cstatic_cache/hashes.json";

    // [build.minify]
    bool minify_css  = true;
    bool minify_js   = true;
    bool minify_html = true;

    // [build.images]
    bool images_optimize  = false;
    int  images_max_width = 1920;
    int  images_quality   = 85;
    bool images_webp      = false;
    bool images_avif      = false;

    // [build]
    bool fingerprint_assets = false;
    bool publish_future     = false;  // skip pages dated in the future

    // [build.search]
    bool        search_enabled = false;
    std::string search_output   = "search-index.json";

    // [build.highlight]
    bool        highlight_enabled = true;
    std::string highlight_style   = "github";   // CSS theme name

    // [build.markdown]
    // GFM extensions to enable. Empty = all four enabled (table, tasklist,
    // strikethrough, autolink). Otherwise only the listed extensions are on.
    std::vector<std::string> markdown_extensions;

    // [build.markdown.shortcodes]
    // Directory (relative to project root) containing shortcode templates.
    // Empty = shortcodes disabled. Default "shortcodes" matches the scaffold.
    std::string shortcodes_dir = "shortcodes";

    // [build.markdown.wikilinks]
    // When true, `[[target]]` and `[[target|display]]` in markdown are
    // rewritten to <a href> before render_markdown. Targets resolve by
    // filename stem, lowercase title, or frontmatter alias. Each page's
    // render context gains `page.backlinks` (array of {url, title}).
    bool wikilinks_enabled = false;

    // [og_images]
    bool        og_images_enabled       = false;
    std::string og_images_template      = "og-default";   // SVG template name (no .svg)
    std::string og_images_output_format = "png";          // "png" or "svg"
    int         og_images_width         = 1200;
    int         og_images_height        = 630;
    std::string og_images_output_dir    = "og";           // subdir under output/

    // [modules]
    bool module_sitemap  = true;
    bool module_rss      = false;
    bool module_json_feed = false;
    bool module_robots   = false;
    bool module_sitemap_ai = false;

    // [modules.rss] (used only when module_rss = true; also feeds JSON Feed)
    int         rss_item_count = 20;
    std::string rss_title;
    std::string rss_description;

    // [modules.json_feed] (used only when module_json_feed = true)
    std::string json_feed_output = "feed.json";  // filename under output_dir

    // [modules.llms_txt] (used only when module_llms_txt = true)
    // Emits llms.txt (compact, honors max_pages) and llms-full.txt (every
    // non-excluded page) per the emerging llms.txt spec for LLM crawlers.
    bool        module_llms_txt       = false;
    std::string llms_txt_description;   // site summary; falls back to site_description
    int         llms_txt_max_pages    = 0;   // 0 = no cap (only affects llms.txt)
    std::vector<std::string> llms_txt_exclude;  // glob patterns matched against page URLs

    // [modules.robots]
    std::string robots_user_agent = "*";
    bool        robots_include_sitemap = true;
    std::vector<std::string> robots_disallow;

    // [modules.robots.ai_crawlers]
    // Known AI/LLM crawlers (GPTBot, ClaudeBot, PerplexityBot, ...). Mode is
    // "off" (emit nothing — default, preserves single-block robots.txt),
    // "allow" (emit `Allow: /` for each known agent), "disallow" (emit
    // `Disallow: /` for each), or "custom" (emit `Allow: /` only for the
    // agents listed in robots_ai_crawlers_custom).
    std::string robots_ai_crawlers_mode = "off";
    std::vector<std::string> robots_ai_crawlers_custom;

    // [seo.json_ld] — emit Schema.org JSON-LD structured data <script> blocks
    // (opt-in; off preserves existing output). When on, {{ seo_meta }} gains
    // WebSite + Organization (if org_name set) + per-page + BreadcrumbList
    // (nested pages) + schema_extra blocks.
    bool json_ld_enabled = false;

    // [seo.organization] — emitted site-wide when org_name is non-empty.
    std::string org_name;
    std::string org_legal_name;
    std::string org_logo;
    std::string org_founding_date;
    std::vector<std::string> org_founders;
    std::vector<std::string> org_same_as;
    std::string org_url;  // defaults to site_base_url when empty

    // [seo.website] — site-wide WebSite schema tuning.
    std::string website_search_url_template;  // e.g. "/search?q={search_term_string}"

    // [seo.citation_tags] — emit citation_* meta tags (Google Scholar,
    // Perplexity, ChatGPT). Opt-in; off preserves existing output.
    bool citation_tags_enabled = false;

    // [authors] — E-E-A-T author entity system (G6). When enabled, .md files
    // under authors_dir are loaded into an index; page frontmatter `author:
    // <slug>` resolves to a full author object (templates) and a Person
    // schema (JSON-LD). Per-author profile pages are generated at
    // /<authors_dir_basename>/<slug>/ using the "author" template.
    bool        authors_enabled = false;
    std::string authors_dir     = "src/authors";

    // [sitemap]
    std::vector<std::string> sitemap_exclude;

    // [sitemap_ai] (used only when module_sitemap_ai = true)
    // Curated sitemap-ai.xml that filters out thin pages (taxonomy listings,
    // paginated indexes, low word-count pages) for AI crawlers.
    bool        sitemap_ai_include_images = true;
    std::vector<std::string> sitemap_ai_exclude_types;  // page.type values to drop

    // [hooks]
    std::string hook_before_build;
    std::string hook_after_build;

    // [check] — `cstatic check` broken-link verifier
    bool check_external   = false;  // also verify external links via HTTP HEAD
    int  check_timeout_ms = 5000;   // per-request timeout for external checks

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

    // [[pagination]] — array of tables for markdown page pagination
    struct PaginationRule {
        std::string source;      // "posts" — match pages under src/posts/
        int per_page = 10;       // items per paginated page
        std::string template_;   // "posts" — template for paginated index
    };
    std::vector<PaginationRule> pagination_rules;

    // [[collection]] — array of tables for content collections
    struct Collection {
        std::string name;           // "posts" — matches src/posts/ directory
        std::string template_;      // "post" — default layout for items
        std::string index_template; // "posts-index" — layout for index page
        std::string url_pattern;    // "/posts/{{ slug }}/" (empty = auto from file path)
        std::string sort_by = "date";       // frontmatter field to sort by
        std::string sort_order = "desc";    // "desc" or "asc"
    };
    std::vector<Collection> collections;

    // [[taxonomy]] — array of tables for automatic tag/category index pages
    struct Taxonomy {
        std::string key;            // "tags" — frontmatter field to index
        std::string template_;      // "tag" — template for term pages
        std::string index_template; // "tags" — template for the taxonomy index page
    };
    std::vector<Taxonomy> taxonomies;
};

// Load and validate config from the given TOML file path.
// Throws ConfigError on any problem (file missing, bad TOML, invalid values).
Config load_config(const std::string& path, const std::string& env = "");

// Serialize config to JSON string (for cache/debugging).
std::string config_to_json(const Config& cfg);

} // namespace cstatic
