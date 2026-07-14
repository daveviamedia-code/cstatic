#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "config/config.hpp"
#include "modules/sitemap_ai.hpp"
#include "utils/file_io.hpp"
#include "test_util.hpp"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using cstatic::Config;
using cstatic::modules::generate_sitemap_ai;
using cstatic::utils::read_file;

namespace {

// RAII temp dir for sitemap-ai output.
struct AiDir {
    fs::path dir;
    AiDir() : dir(cstatic_test::unique_temp_dir("cstatic_sitemap_ai_")) {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~AiDir() { fs::remove_all(dir); }
    std::string path() const { return dir.string(); }
    std::string read() const {
        return read_file((dir / "sitemap-ai.xml").string());
    }
};

static bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

static int count_of(const std::string& h, const std::string& needle) {
    int n = 0;
    size_t pos = 0;
    while ((pos = h.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
}

static Config base_config() {
    Config cfg;
    cfg.site_title = "AI Site";
    cfg.site_base_url = "https://example.com";
    cfg.module_sitemap_ai = true;
    return cfg;
}

static nlohmann::json make_page(const std::string& url, int word_count,
                                const std::string& title = "P") {
    nlohmann::json p;
    p["title"] = title;
    p["url"] = url;
    p["word_count"] = word_count;
    return p;
}

} // anonymous namespace

TEST_CASE("sitemap_ai: emits only prose pages (word_count > 100)", "[sitemap_ai]") {
    AiDir d;
    Config cfg = base_config();
    nlohmann::json pages = nlohmann::json::array({
        make_page("/posts/hello/", 500, "Hello"),
        make_page("/posts/thin/", 50, "Thin"),
        make_page("/about/", 0, "About"),
    });
    generate_sitemap_ai(cfg, pages, d.path());
    std::string xml = d.read();
    REQUIRE(contains(xml, "/posts/hello/"));
    REQUIRE_FALSE(contains(xml, "/posts/thin/"));
    REQUIRE_FALSE(contains(xml, "/about/"));
}

TEST_CASE("sitemap_ai: excludes thin URLs (/tags/, /page/N/)", "[sitemap_ai]") {
    AiDir d;
    Config cfg = base_config();
    nlohmann::json pages = nlohmann::json::array({
        make_page("/posts/hello/", 500, "Hello"),
        make_page("/tags/tech/", 500, "Tag"),
        make_page("/categories/news/", 500, "Cat"),
        make_page("/page/2/", 500, "Page2"),
    });
    generate_sitemap_ai(cfg, pages, d.path());
    std::string xml = d.read();
    REQUIRE(contains(xml, "/posts/hello/"));
    REQUIRE_FALSE(contains(xml, "/tags/tech/"));
    REQUIRE_FALSE(contains(xml, "/categories/news/"));
    REQUIRE_FALSE(contains(xml, "/page/2/"));
}

TEST_CASE("sitemap_ai: respects inherited sitemap_exclude", "[sitemap_ai]") {
    AiDir d;
    Config cfg = base_config();
    cfg.sitemap_exclude = {"/private/*"};
    nlohmann::json pages = nlohmann::json::array({
        make_page("/posts/hello/", 500, "Hello"),
        make_page("/private/secret/", 500, "Secret"),
    });
    generate_sitemap_ai(cfg, pages, d.path());
    std::string xml = d.read();
    REQUIRE(contains(xml, "/posts/hello/"));
    REQUIRE_FALSE(contains(xml, "/private/secret/"));
}

TEST_CASE("sitemap_ai: type-based exclusion", "[sitemap_ai]") {
    AiDir d;
    Config cfg = base_config();
    cfg.sitemap_ai_exclude_types = {"landing"};
    nlohmann::json pages = nlohmann::json::array({
        make_page("/posts/hello/", 500, "Hello"),
        make_page("/landing/", 500, "Landing"),
    });
    pages[1]["type"] = "landing";
    generate_sitemap_ai(cfg, pages, d.path());
    std::string xml = d.read();
    REQUIRE(contains(xml, "/posts/hello/"));
    REQUIRE_FALSE(contains(xml, "/landing/"));
}

TEST_CASE("sitemap_ai: image entries present when include_images=true", "[sitemap_ai]") {
    AiDir d;
    Config cfg = base_config();
    cfg.sitemap_ai_include_images = true;
    nlohmann::json pages = nlohmann::json::array({
        make_page("/posts/hello/", 500, "Hello"),
    });
    pages[0]["og_image"] = "/og/hello.png";
    pages[0]["image"] = "https://cdn.example.com/hero.jpg";
    generate_sitemap_ai(cfg, pages, d.path());
    std::string xml = d.read();
    REQUIRE(contains(xml, "xmlns:image="));
    REQUIRE(contains(xml, "<image:image>"));
    // og_image resolved relative to base_url
    REQUIRE(contains(xml, "https://example.com/og/hello.png"));
    // image already absolute, used as-is
    REQUIRE(contains(xml, "https://cdn.example.com/hero.jpg"));
    // deduped — if both fields point to the same URL, only one entry
    int img_count = count_of(xml, "<image:image>");
    REQUIRE(img_count == 2);
}

TEST_CASE("sitemap_ai: no image entries when include_images=false", "[sitemap_ai]") {
    AiDir d;
    Config cfg = base_config();
    cfg.sitemap_ai_include_images = false;
    nlohmann::json pages = nlohmann::json::array({
        make_page("/posts/hello/", 500, "Hello"),
    });
    pages[0]["og_image"] = "/og/hello.png";
    generate_sitemap_ai(cfg, pages, d.path());
    std::string xml = d.read();
    REQUIRE_FALSE(contains(xml, "xmlns:image="));
    REQUIRE_FALSE(contains(xml, "<image:image>"));
}

TEST_CASE("sitemap_ai: no image namespace when no images on any included page", "[sitemap_ai]") {
    AiDir d;
    Config cfg = base_config();
    cfg.sitemap_ai_include_images = true;
    nlohmann::json pages = nlohmann::json::array({
        make_page("/posts/hello/", 500, "Hello"),
    });
    // page has no og_image or image fields
    generate_sitemap_ai(cfg, pages, d.path());
    std::string xml = d.read();
    REQUIRE_FALSE(contains(xml, "xmlns:image="));
    REQUIRE_FALSE(contains(xml, "<image:image>"));
    // but the page itself is still present
    REQUIRE(contains(xml, "/posts/hello/"));
}
