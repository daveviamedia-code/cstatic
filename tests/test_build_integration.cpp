#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <unistd.h>

#include "config/config.hpp"
#include "pipeline/builder.hpp"
#include "utils/file_io.hpp"

namespace fs = std::filesystem;

using namespace cstatic;

struct BuildFixture {
    std::string root_dir;
    std::string saved_cwd;

    BuildFixture() {
        // Save CWD
        char buf[4096];
        saved_cwd = getcwd(buf, sizeof(buf));

        // Create temp directory
        root_dir = fs::temp_directory_path() / ("cstatic_test_" + std::to_string(std::rand()));
        fs::create_directories(root_dir);
        fs::create_directories(root_dir + "/src");
        fs::create_directories(root_dir + "/templates");
        fs::create_directories(root_dir + "/static");
        fs::create_directories(root_dir + "/_data");

        // Write a minimal config.toml (build_site reads this for hash)
        cstatic::utils::write_file(root_dir + "/config.toml",
            "[site]\ntitle = \"Test Site\"\nbase_url = \"https://example.com\"\n"
            "language = \"en\"\n");

        // Change to temp dir
        chdir(root_dir.c_str());
    }

    ~BuildFixture() {
        chdir(saved_cwd.c_str());
        fs::remove_all(root_dir);
    }

    void write_source(const std::string& name, const std::string& content) {
        std::string path = root_dir + "/src/" + name;
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream f(path);
        f << content;
    }

    void write_template(const std::string& name, const std::string& content) {
        std::ofstream f(root_dir + "/templates/" + name);
        f << content;
    }

    void write_data(const std::string& name, const std::string& content) {
        std::ofstream f(root_dir + "/_data/" + name);
        f << content;
    }

    bool output_exists(const std::string& rel_path) {
        return fs::exists(root_dir + "/output/" + rel_path);
    }

    std::string read_output(const std::string& rel_path) {
        try {
            return cstatic::utils::read_file(root_dir + "/output/" + rel_path);
        } catch (...) {
            return "";
        }
    }

    Config make_config() const {
        Config cfg;
        cfg.site_title = "Test Site";
        cfg.site_base_url = "https://example.com";
        cfg.site_language = "en";
        cfg.source_dir = root_dir + "/src";
        cfg.output_dir = root_dir + "/output";
        cfg.template_dir = root_dir + "/templates";
        cfg.static_dir = root_dir + "/static";
        cfg.data_dir = root_dir + "/_data";
        cfg.incremental_enabled = true;
        cfg.incremental_hash_file = root_dir + "/.cstatic_cache/hashes.json";
        return cfg;
    }
};

TEST_CASE_METHOD(BuildFixture, "Integration: basic build produces HTML", "[integration]") {
    write_source("index.md",
        "---\ntitle: Home\n---\n# Welcome\nHello world.\n");
    write_template("default.html",
        "<html><head><title>{{ page.title }}</title></head>"
        "<body>{{ page.content }}</body></html>");

    auto result = build_site(make_config(), true);
    REQUIRE(result.pages_built >= 1);
    REQUIRE(output_exists("index.html"));

    std::string html = read_output("index.html");
    REQUIRE(html.find("Welcome") != std::string::npos);
    REQUIRE(html.find("Home") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Integration: incremental build caches pages", "[integration]") {
    write_source("index.md",
        "---\ntitle: Home\n---\nContent.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto cfg = make_config();

    // First build
    auto r1 = build_site(cfg, true);
    REQUIRE(r1.pages_built >= 1);

    // Second build — same files, should be cached
    auto r2 = build_site(cfg, false);
    REQUIRE(r2.pages_cached > 0);
}

TEST_CASE_METHOD(BuildFixture, "Integration: draft pages are excluded", "[integration]") {
    write_source("index.md",
        "---\ntitle: Public\n---\nPublic content.\n");
    write_source("draft.md",
        "---\ntitle: Draft\ndraft: true\n---\nDraft content.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto result = build_site(make_config(), true);
    REQUIRE(result.pages_skipped == 1);
    REQUIRE_FALSE(output_exists("draft/index.html"));
    REQUIRE(output_exists("index.html"));
}

TEST_CASE_METHOD(BuildFixture, "Integration: --drafts includes draft pages", "[integration]") {
    write_source("index.md",
        "---\ntitle: Public\n---\nPublic content.\n");
    write_source("draft.md",
        "---\ntitle: Draft\ndraft: true\n---\nDraft content.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto result = build_site(make_config(), true, true);
    REQUIRE(result.pages_skipped == 0);
    REQUIRE(output_exists("index.html"));
    REQUIRE(output_exists("draft/index.html"));

    std::string html = read_output("draft/index.html");
    REQUIRE(html.find("Draft content") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Integration: data-driven per-item pages", "[integration]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_template("product.html",
        "<html><body><h1>{{ item.name }}</h1></body></html>");
    write_data("products.json",
        "[{\"name\": \"Widget\", \"slug\": \"widget\"},"
        " {\"name\": \"Gadget\", \"slug\": \"gadget\"}]");

    auto cfg = make_config();
    Config::DataSource ds;
    ds.file = "products.json";
    ds.template_name = "product";
    ds.url_pattern = "/products/{{ slug }}/";
    ds.item_key = "slug";
    ds.per_item = true;
    cfg.data_sources.push_back(ds);

    auto result = build_site(cfg, true);
    REQUIRE(result.pages_built >= 2);
    REQUIRE(output_exists("products/widget/index.html"));
    REQUIRE(output_exists("products/gadget/index.html"));

    std::string html = read_output("products/widget/index.html");
    REQUIRE(html.find("Widget") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Integration: 404 page generated", "[integration]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    build_site(make_config(), true);

    // Built-in 404 page should be generated when no src/404.md exists
    REQUIRE(output_exists("404.html"));
    std::string html = read_output("404.html");
    REQUIRE(html.find("404") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Integration: markdown pagination", "[integration]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_source("posts/first.md",
        "---\ntitle: First Post\ndate: 2024-01-01\n---\nFirst.\n");
    write_source("posts/second.md",
        "---\ntitle: Second Post\ndate: 2024-01-02\n---\nSecond.\n");
    write_source("posts/third.md",
        "---\ntitle: Third Post\ndate: 2024-01-03\n---\nThird.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");
    write_template("posts.html",
        "<html><body>"
        "{% for item in pagination.items %}"
        "<a href=\"{{ item.url }}\">{{ item.title }}</a>"
        "{% endfor %}"
        "{% if pagination.prev_url %}<a href=\"{{ pagination.prev_url }}\">Prev</a>{% endif %}"
        "{% if pagination.next_url %}<a href=\"{{ pagination.next_url }}\">Next</a>{% endif %}"
        "</body></html>");

    auto cfg = make_config();
    Config::PaginationRule pr;
    pr.source = "posts";
    pr.template_ = "posts";
    pr.per_page = 2;
    cfg.pagination_rules.push_back(pr);

    auto result = build_site(cfg, true);

    // Should have 4 source pages + 2 paginated index pages
    REQUIRE(result.pages_built >= 6);

    // First page at /posts/ with 2 items (sorted by date descending)
    REQUIRE(output_exists("posts/index.html"));
    std::string p1 = read_output("posts/index.html");
    REQUIRE(p1.find("Third Post") != std::string::npos);
    REQUIRE(p1.find("Second Post") != std::string::npos);
    REQUIRE(p1.find("Next") != std::string::npos);

    // Second page at /posts/page/2/ with 1 item
    REQUIRE(output_exists("posts/page/2/index.html"));
    std::string p2 = read_output("posts/page/2/index.html");
    REQUIRE(p2.find("First Post") != std::string::npos);
    REQUIRE(p2.find("Prev") != std::string::npos);
}
