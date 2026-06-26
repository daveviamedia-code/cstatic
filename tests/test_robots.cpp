#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "config/config.hpp"
#include "modules/robots.hpp"
#include "utils/file_io.hpp"

namespace fs = std::filesystem;
using cstatic::Config;

namespace {

// Count (case-sensitive) occurrences of needle in haystack.
static int count_of(const std::string& haystack, const std::string& needle) {
    int n = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
}

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Build a Config with only the fields robots.txt generation reads.
static Config base_config() {
    Config cfg;
    cfg.module_robots = true;
    cfg.module_sitemap = true;
    cfg.site_base_url = "https://example.com";
    cfg.robots_include_sitemap = true;
    return cfg;
}

// RAII temp dir for robots output.
struct RobotsDir {
    fs::path dir;
    RobotsDir() : dir(fs::temp_directory_path() / "cstatic_robots_test") {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~RobotsDir() { fs::remove_all(dir); }
    std::string path() const { return dir.string(); }
    std::string read() const {
        return cstatic::utils::read_file((dir / "robots.txt").string());
    }
};

} // anonymous namespace

TEST_CASE("Robots: off mode preserves single-block behavior", "[robots]") {
    RobotsDir d;
    Config cfg = base_config();
    cfg.robots_ai_crawlers_mode = "off";
    cfg.robots_disallow = {"/private/"};

    cstatic::modules::generate_robots(cfg, d.path());
    const std::string out = d.read();

    REQUIRE(count_of(out, "User-agent:") == 1);
    REQUIRE(contains(out, "User-agent: *\n"));
    REQUIRE(contains(out, "Disallow: /private/\n"));
    REQUIRE(!contains(out, "GPTBot"));
    REQUIRE(!contains(out, "Allow: /\n"));
    REQUIRE(contains(out, "Sitemap: https://example.com/sitemap.xml"));
}

TEST_CASE("Robots: default mode (unset) preserves single-block behavior", "[robots]") {
    RobotsDir d;
    Config cfg = base_config(); // robots_ai_crawlers_mode defaults to "off"

    cstatic::modules::generate_robots(cfg, d.path());
    const std::string out = d.read();

    REQUIRE(count_of(out, "User-agent:") == 1);
    REQUIRE(!contains(out, "GPTBot"));
}

TEST_CASE("Robots: allow mode emits Allow:/ for every known AI agent", "[robots]") {
    RobotsDir d;
    Config cfg = base_config();
    cfg.robots_ai_crawlers_mode = "allow";

    cstatic::modules::generate_robots(cfg, d.path());
    const std::string out = d.read();

    // 1 main block + 12 AI agents.
    REQUIRE(count_of(out, "User-agent:") == 13);
    REQUIRE(count_of(out, "Allow: /\n") == 12);

    // Representative agents from the known list.
    for (const char* agent : {"GPTBot", "OAI-SearchBot", "ClaudeBot",
                              "PerplexityBot", "Perplexity-User", "CCBot",
                              "Google-Extended", "Applebot-Extended",
                              "Meta-ExternalAgent", "Amazonbot",
                              "Bytespider", "Diffbot"}) {
        REQUIRE(contains(out, std::string("User-agent: ") + agent + "\n"));
    }
    // Sitemap still present and no blanket Disallow of agents.
    REQUIRE(contains(out, "Sitemap: https://example.com/sitemap.xml"));
    REQUIRE(!contains(out, "Disallow: /\n"));
}

TEST_CASE("Robots: disallow mode emits Disallow:/ for every known AI agent", "[robots]") {
    RobotsDir d;
    Config cfg = base_config();
    cfg.robots_ai_crawlers_mode = "disallow";

    cstatic::modules::generate_robots(cfg, d.path());
    const std::string out = d.read();

    REQUIRE(count_of(out, "User-agent:") == 13);
    REQUIRE(count_of(out, "Disallow: /\n") == 12);
    REQUIRE(contains(out, "User-agent: GPTBot\nDisallow: /\n"));
    REQUIRE(contains(out, "User-agent: ClaudeBot\nDisallow: /\n"));
    REQUIRE(!contains(out, "Allow: /\n"));
}

TEST_CASE("Robots: custom mode emits only listed agents", "[robots]") {
    RobotsDir d;
    Config cfg = base_config();
    cfg.robots_ai_crawlers_mode = "custom";
    cfg.robots_ai_crawlers_custom = {"GPTBot", "ClaudeBot"};

    cstatic::modules::generate_robots(cfg, d.path());
    const std::string out = d.read();

    // 1 main block + 2 listed agents.
    REQUIRE(count_of(out, "User-agent:") == 3);
    REQUIRE(contains(out, "User-agent: GPTBot\nAllow: /\n"));
    REQUIRE(contains(out, "User-agent: ClaudeBot\nAllow: /\n"));
    // An unlisted known agent must NOT appear.
    REQUIRE(!contains(out, "PerplexityBot"));
    REQUIRE(!contains(out, "CCBot"));
}

TEST_CASE("Robots: custom mode with empty list emits no AI blocks", "[robots]") {
    RobotsDir d;
    Config cfg = base_config();
    cfg.robots_ai_crawlers_mode = "custom"; // custom list left empty

    cstatic::modules::generate_robots(cfg, d.path());
    const std::string out = d.read();

    REQUIRE(count_of(out, "User-agent:") == 1);
    REQUIRE(!contains(out, "GPTBot"));
}

TEST_CASE("Robots: AI blocks absent when sitemap disabled too", "[robots]") {
    RobotsDir d;
    Config cfg = base_config();
    cfg.robots_ai_crawlers_mode = "allow";
    cfg.module_sitemap = false;       // no Sitemap line
    cfg.robots_include_sitemap = false;

    cstatic::modules::generate_robots(cfg, d.path());
    const std::string out = d.read();

    REQUIRE(count_of(out, "User-agent:") == 13);
    REQUIRE(!contains(out, "Sitemap:"));
}
