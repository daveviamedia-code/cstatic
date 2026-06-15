#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <cstdlib>

#include "config/config.hpp"

namespace fs = std::filesystem;

// Helper: write a config file to a temp path, return the path.
static std::string write_temp_config(const std::string& contents, const std::string& name = "test_config.toml") {
    std::ofstream f(name);
    f << contents;
    f.close();
    return name;
}

struct ConfigFixture {
    std::string config_path;
    ConfigFixture() : config_path("test_config.toml") {}
    ~ConfigFixture() {
        fs::remove(config_path);
        // Also remove the output/src/static/templates dirs if created
        // (only if they exist and are empty enough)
    }
};

TEST_CASE_METHOD(ConfigFixture, "Config: valid minimal config", "[config]") {
    std::string path = write_temp_config(R"(
[site]
title = "Test Site"
base_url = "https://example.com"
)");
    auto cfg = cstatic::load_config(path);
    REQUIRE(cfg.site_title == "Test Site");
    REQUIRE(cfg.site_base_url == "https://example.com");
    REQUIRE(cfg.site_language == "en"); // default
    REQUIRE(cfg.source_dir == "src");   // default
    REQUIRE(cfg.output_dir == "output"); // default
    fs::remove(path);
}

TEST_CASE_METHOD(ConfigFixture, "Config: missing required site.title", "[config]") {
    std::string path = write_temp_config(R"(
[site]
base_url = "https://example.com"
)");
    REQUIRE_THROWS_AS(cstatic::load_config(path), cstatic::ConfigError);
    fs::remove(path);
}

TEST_CASE_METHOD(ConfigFixture, "Config: missing required site.base_url", "[config]") {
    std::string path = write_temp_config(R"(
[site]
title = "Test Site"
)");
    REQUIRE_THROWS_AS(cstatic::load_config(path), cstatic::ConfigError);
    fs::remove(path);
}

TEST_CASE_METHOD(ConfigFixture, "Config: base_url without scheme", "[config]") {
    std::string path = write_temp_config(R"(
[site]
title = "Test Site"
base_url = "example.com"
)");
    REQUIRE_THROWS_AS(cstatic::load_config(path), cstatic::ConfigError);
    fs::remove(path);
}

TEST_CASE_METHOD(ConfigFixture, "Config: base_url trailing slash stripped", "[config]") {
    std::string path = write_temp_config(R"(
[site]
title = "Test Site"
base_url = "https://example.com/"
)");
    auto cfg = cstatic::load_config(path);
    REQUIRE(cfg.site_base_url == "https://example.com");
    fs::remove(path);
}

TEST_CASE_METHOD(ConfigFixture, "Config: invalid TOML syntax", "[config]") {
    std::string path = write_temp_config(R"(this is not valid toml [[[)");
    REQUIRE_THROWS_AS(cstatic::load_config(path), cstatic::ConfigError);
    fs::remove(path);
}

TEST_CASE_METHOD(ConfigFixture, "Config: type mismatch on site.title", "[config]") {
    std::string path = write_temp_config(R"(
[site]
title = 123
base_url = "https://example.com"
)");
    REQUIRE_THROWS_AS(cstatic::load_config(path), cstatic::ConfigError);
    fs::remove(path);
}

TEST_CASE_METHOD(ConfigFixture, "Config: full config with all options", "[config]") {
    std::string path = write_temp_config(R"(
[site]
title = "Full Site"
base_url = "https://full.example.com"
language = "fr"

[build]
source_dir = "content"
output_dir = "dist"
template_dir = "layouts"
static_dir = "assets"

[build.incremental]
enabled = false
hash_file = ".cache/hashes.json"

[build.minify]
css = false
js = false

[modules]
sitemap = false
rss = true
robots = true
rss_title = "Full RSS"
rss_description = "Full feed"
rss_item_count = 50
robots_user_agent = "GoogleBot"
robots_include_sitemap = false
robots_disallow = ["/admin", "/private"]

[data]
data_dir = "data"

[[data_source]]
file = "products.json"
template = "product"
url_pattern = "/products/{{ slug }}/"
item_key = "slug"
per_page = 10
per_item = true
)");
    auto cfg = cstatic::load_config(path);
    REQUIRE(cfg.site_title == "Full Site");
    REQUIRE(cfg.site_language == "fr");
    REQUIRE(cfg.source_dir == "content");
    REQUIRE(cfg.output_dir == "dist");
    REQUIRE(cfg.template_dir == "layouts");
    REQUIRE(cfg.static_dir == "assets");
    REQUIRE(cfg.incremental_enabled == false);
    REQUIRE(cfg.minify_css == false);
    REQUIRE(cfg.minify_js == false);
    REQUIRE(cfg.module_sitemap == false);
    REQUIRE(cfg.module_rss == true);
    REQUIRE(cfg.module_robots == true);
    REQUIRE(cfg.rss_item_count == 50);
    REQUIRE(cfg.rss_title == "Full RSS");
    REQUIRE(cfg.robots_disallow.size() == 2);
    REQUIRE(cfg.data_sources.size() == 1);
    REQUIRE(cfg.data_sources[0].file == "products.json");
    REQUIRE(cfg.data_sources[0].per_page == 10);
    REQUIRE(cfg.data_sources[0].per_item == true);
    fs::remove(path);
}

TEST_CASE_METHOD(ConfigFixture, "Config: missing config file", "[config]") {
    REQUIRE_THROWS_AS(cstatic::load_config("nonexistent_config.toml"), cstatic::ConfigError);
}

TEST_CASE_METHOD(ConfigFixture, "Config: defaults applied correctly", "[config]") {
    std::string path = write_temp_config(R"(
[site]
title = "Defaults Test"
base_url = "https://example.com"
)");
    auto cfg = cstatic::load_config(path);
    REQUIRE(cfg.site_language == "en");
    REQUIRE(cfg.source_dir == "src");
    REQUIRE(cfg.output_dir == "output");
    REQUIRE(cfg.template_dir == "templates");
    REQUIRE(cfg.static_dir == "static");
    REQUIRE(cfg.incremental_enabled == true);
    REQUIRE(cfg.minify_css == true);
    REQUIRE(cfg.minify_js == true);
    REQUIRE(cfg.module_sitemap == true);
    REQUIRE(cfg.module_rss == false);
    REQUIRE(cfg.module_robots == false);
    REQUIRE(cfg.data_dir == "_data");
    fs::remove(path);
}

TEST_CASE_METHOD(ConfigFixture, "Config: env default is development", "[config]") {
    std::string path = write_temp_config(R"(
[site]
title = "Env Test"
base_url = "https://example.com"
)");
    auto cfg = cstatic::load_config(path);
    REQUIRE(cfg.env == "development");
    fs::remove(path);
}

TEST_CASE_METHOD(ConfigFixture, "Config: env overlay merges", "[config]") {
    std::string path = write_temp_config(R"(
[site]
title = "Base Title"
base_url = "https://example.com"
)");
    // Write overlay: override base_url but leave title alone.
    std::string overlay_path = "test_config.production.toml";
    {
        std::ofstream f(overlay_path);
        f << R"([site]
base_url = "https://prod.example.com"
)";
    }

    auto cfg = cstatic::load_config(path, "production");
    REQUIRE(cfg.env == "production");
    REQUIRE(cfg.site_title == "Base Title");     // unchanged from base
    REQUIRE(cfg.site_base_url == "https://prod.example.com");  // overridden

    fs::remove(path);
    fs::remove(overlay_path);
}

TEST_CASE_METHOD(ConfigFixture, "Config: missing overlay warns but continues", "[config]") {
    std::string path = write_temp_config(R"(
[site]
title = "No Overlay"
base_url = "https://example.com"
)");
    auto cfg = cstatic::load_config(path, "staging");
    REQUIRE(cfg.env == "staging");
    REQUIRE(cfg.site_title == "No Overlay");
    fs::remove(path);
}
