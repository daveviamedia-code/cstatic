#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <cstdlib>

#include <nlohmann/json.hpp>

#include "config/config.hpp"
#include "pipeline/builder.hpp"
#include "modules/og_images.hpp"
#include "utils/file_io.hpp"
#include "test_util.hpp"

namespace fs = std::filesystem;

using namespace cstatic;

struct BuildFixture {
    std::string root_dir;
    std::string saved_cwd;

    BuildFixture() {
        // Save CWD
        saved_cwd = fs::current_path().string();

        // Create temp directory
        root_dir = cstatic_test::unique_temp_dir("cstatic_test_");
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
        fs::current_path(root_dir);
    }

    ~BuildFixture() {
        fs::current_path(saved_cwd);
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

    std::string write_hook(const std::string& name, const std::string& body) {
        std::string path = root_dir + "/" + name;
        std::ofstream f(path);
        f << "#!/bin/bash\n" << body;
        f.close();
        std::string cmd = "chmod +x \"" + path + "\" 2>/dev/null";
        std::system(cmd.c_str());
        return path;
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

TEST_CASE_METHOD(BuildFixture, "Integration: future-dated pages are skipped by default", "[integration]") {
    write_source("index.md",
        "---\ntitle: Home\n---\nPublic content.\n");
    write_source("upcoming.md",
        "---\ntitle: Upcoming\ndate: 2999-01-01\n---\nFuture content.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto result = build_site(make_config(), true);
    REQUIRE(result.pages_scheduled == 1);
    REQUIRE(result.pages_skipped == 0);
    REQUIRE(output_exists("index.html"));
    REQUIRE_FALSE(output_exists("upcoming/index.html"));
}

TEST_CASE_METHOD(BuildFixture, "Integration: publish_future includes future-dated pages", "[integration]") {
    write_source("index.md",
        "---\ntitle: Home\n---\nPublic content.\n");
    write_source("upcoming.md",
        "---\ntitle: Upcoming\ndate: 2999-01-01\n---\nFuture content.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto cfg = make_config();
    cfg.publish_future = true;
    auto result = build_site(cfg, true);
    REQUIRE(result.pages_scheduled == 0);
    REQUIRE(output_exists("index.html"));
    REQUIRE(output_exists("upcoming/index.html"));
}

TEST_CASE_METHOD(BuildFixture, "Integration: include_drafts bypasses scheduling for preview", "[integration]") {
    write_source("upcoming.md",
        "---\ntitle: Upcoming\ndate: 2999-01-01\n---\nFuture content.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    // include_drafts=true (dev server / --drafts) previews scheduled content.
    auto result = build_site(make_config(), true, true);
    REQUIRE(result.pages_scheduled == 0);
    REQUIRE(output_exists("upcoming/index.html"));
}

TEST_CASE_METHOD(BuildFixture, "Integration: past-dated pages are never scheduled", "[integration]") {
    write_source("post.md",
        "---\ntitle: Old Post\ndate: 2000-01-01\n---\nPast content.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto result = build_site(make_config(), true);
    REQUIRE(result.pages_scheduled == 0);
    REQUIRE(output_exists("post/index.html"));
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

TEST_CASE_METHOD(BuildFixture, "Integration: collection default template", "[integration][collection]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_source("posts/first.md",
        "---\ntitle: First Post\ndate: 2024-01-01\n---\nFirst post body.\n");
    write_template("default.html",
        "<html><body>DEFAULT: {{ page.content }}</body></html>");
    write_template("post.html",
        "<html><body>POST: {{ page.content }}</body></html>");

    auto cfg = make_config();
    Config::Collection col;
    col.name = "posts";
    col.template_ = "post";
    col.index_template = "posts-index";
    col.sort_by = "date";
    col.sort_order = "desc";
    cfg.collections.push_back(col);

    auto result = build_site(cfg, true);
    REQUIRE(result.pages_built >= 2);

    // The post should use "post" template (not "default")
    std::string html = read_output("posts/first/index.html");
    REQUIRE(html.find("POST:") != std::string::npos);
    REQUIRE(html.find("First post body") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Integration: collection URL pattern", "[integration][collection]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_source("posts/my-first.md",
        "---\ntitle: First Post\ndate: 2024-01-01\n---\nFirst.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");
    write_template("post.html",
        "<html><body>{{ page.content }}</body></html>");

    auto cfg = make_config();
    Config::Collection col;
    col.name = "posts";
    col.template_ = "post";
    col.index_template = "posts-index";
    col.url_pattern = "/blog/{{ slug }}/";
    cfg.collections.push_back(col);

    auto result = build_site(cfg, true);
    REQUIRE(result.pages_built >= 2);

    // Output should be at /blog/my-first/ not /posts/my-first/
    REQUIRE(output_exists("blog/my-first/index.html"));
    REQUIRE_FALSE(output_exists("posts/my-first/index.html"));
}

TEST_CASE_METHOD(BuildFixture, "Integration: collection index page", "[integration][collection]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_source("posts/first.md",
        "---\ntitle: First Post\ndate: 2024-01-01\n---\nFirst.\n");
    write_source("posts/second.md",
        "---\ntitle: Second Post\ndate: 2024-01-02\n---\nSecond.\n");
    write_source("posts/third.md",
        "---\ntitle: Third Post\ndate: 2024-01-03\n---\nThird.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");
    write_template("post.html",
        "<html><body>{{ page.content }}</body></html>");
    write_template("posts-index.html",
        "<html><body>"
        "{% for p in collection.pages %}"
        "<a href=\"{{ p.url }}\">{{ p.title }}</a>"
        "{% endfor %}"
        "</body></html>");

    auto cfg = make_config();
    Config::Collection col;
    col.name = "posts";
    col.template_ = "post";
    col.index_template = "posts-index";
    col.sort_by = "date";
    col.sort_order = "desc";
    cfg.collections.push_back(col);

    auto result = build_site(cfg, true);

    // Collection index page at /posts/index.html
    REQUIRE(output_exists("posts/index.html"));

    std::string index = read_output("posts/index.html");
    REQUIRE(index.find("Third Post") != std::string::npos);
    REQUIRE(index.find("Second Post") != std::string::npos);
    REQUIRE(index.find("First Post") != std::string::npos);

    // Verify desc order: Third should appear before First
    auto pos_third = index.find("Third Post");
    auto pos_first = index.find("First Post");
    REQUIRE(pos_third < pos_first);
}

TEST_CASE_METHOD(BuildFixture, "Integration: collections template variable", "[integration][collection]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_source("posts/first.md",
        "---\ntitle: First Post\ndate: 2024-01-01\n---\nFirst.\n");
    write_template("default.html",
        "<html><body>"
        "{% for p in collections.posts %}"
        "<span>{{ p.title }}</span>"
        "{% endfor %}"
        "</body></html>");
    write_template("post.html",
        "<html><body>{{ page.content }}</body></html>");
    write_template("posts-index.html",
        "<html><body></body></html>");

    auto cfg = make_config();
    Config::Collection col;
    col.name = "posts";
    col.template_ = "post";
    col.index_template = "posts-index";
    cfg.collections.push_back(col);

    auto result = build_site(cfg, true);

    // The index page should have collections.posts available
    std::string index = read_output("index.html");
    REQUIRE(index.find("First Post") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Integration: collection sort order asc", "[integration][collection]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_source("posts/alpha.md",
        "---\ntitle: Alpha\ndate: 2024-03-01\n---\nAlpha.\n");
    write_source("posts/beta.md",
        "---\ntitle: Beta\ndate: 2024-01-01\n---\nBeta.\n");
    write_source("posts/gamma.md",
        "---\ntitle: Gamma\ndate: 2024-02-01\n---\nGamma.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");
    write_template("post.html",
        "<html><body>{{ page.content }}</body></html>");
    write_template("posts-index.html",
        "<html><body>"
        "{% for p in collection.pages %}{{ p.title }},{% endfor %}"
        "</body></html>");

    auto cfg = make_config();
    Config::Collection col;
    col.name = "posts";
    col.template_ = "post";
    col.index_template = "posts-index";
    col.sort_by = "date";
    col.sort_order = "asc";
    cfg.collections.push_back(col);

    auto result = build_site(cfg, true);
    REQUIRE(output_exists("posts/index.html"));

    std::string index = read_output("posts/index.html");
    // Ascending: Beta (Jan), Gamma (Feb), Alpha (Mar)
    auto pos_beta = index.find("Beta");
    auto pos_gamma = index.find("Gamma");
    auto pos_alpha = index.find("Alpha");
    REQUIRE(pos_beta < pos_gamma);
    REQUIRE(pos_gamma < pos_alpha);
}

// --- Taxonomy Tests ---

TEST_CASE_METHOD(BuildFixture, "Integration: taxonomy index page", "[integration][taxonomy]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_source("posts/a.md",
        "---\ntitle: Post A\ndate: 2024-01-01\ntags: [webdev, css]\n---\nPost A.\n");
    write_source("posts/b.md",
        "---\ntitle: Post B\ndate: 2024-02-01\ntags: [webdev, rust]\n---\nPost B.\n");
    write_source("posts/c.md",
        "---\ntitle: Post C\ndate: 2024-03-01\ntags: [css, design]\n---\nPost C.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");
    write_template("tag.html",
        "<html><body>"
        "<h1>{{ taxonomy.term }}</h1>"
        "{% for p in taxonomy.pages %}"
        "<a href=\"{{ p.url }}\">{{ p.title }}</a>"
        "{% endfor %}"
        "</body></html>");
    write_template("tags.html",
        "<html><body>"
        "<h1>{{ taxonomy.key }}</h1>"
        "{% for t in taxonomy.terms %}"
        "<a href=\"{{ t.url }}\">{{ t.term }}</a> ({{ t.count }})"
        "{% endfor %}"
        "</body></html>");

    auto cfg = make_config();
    Config::Taxonomy tax;
    tax.key = "tags";
    tax.template_ = "tag";
    tax.index_template = "tags";
    cfg.taxonomies.push_back(tax);

    auto result = build_site(cfg, true);

    // Taxonomy index page at /tags/
    REQUIRE(output_exists("tags/index.html"));
    std::string index = read_output("tags/index.html");
    REQUIRE(index.find("webdev") != std::string::npos);
    REQUIRE(index.find("css") != std::string::npos);
    REQUIRE(index.find("rust") != std::string::npos);
    REQUIRE(index.find("design") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Integration: taxonomy term pages", "[integration][taxonomy]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_source("posts/a.md",
        "---\ntitle: Post A\ndate: 2024-01-01\ntags: [webdev, css]\n---\nPost A.\n");
    write_source("posts/b.md",
        "---\ntitle: Post B\ndate: 2024-02-01\ntags: [webdev, rust]\n---\nPost B.\n");
    write_source("posts/c.md",
        "---\ntitle: Post C\ndate: 2024-03-01\ntags: [css, design]\n---\nPost C.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");
    write_template("tag.html",
        "<html><body>"
        "<h1>{{ taxonomy.term }}</h1>"
        "{% for p in taxonomy.pages %}"
        "<a href=\"{{ p.url }}\">{{ p.title }}</a>"
        "{% endfor %}"
        "</body></html>");
    write_template("tags.html",
        "<html><body></body></html>");

    auto cfg = make_config();
    Config::Taxonomy tax;
    tax.key = "tags";
    tax.template_ = "tag";
    tax.index_template = "tags";
    cfg.taxonomies.push_back(tax);

    auto result = build_site(cfg, true);

    // /tags/webdev/ should list Post A and Post B
    REQUIRE(output_exists("tags/webdev/index.html"));
    std::string webdev = read_output("tags/webdev/index.html");
    REQUIRE(webdev.find("webdev") != std::string::npos);
    REQUIRE(webdev.find("Post A") != std::string::npos);
    REQUIRE(webdev.find("Post B") != std::string::npos);

    // /tags/css/ should list Post A and Post C
    REQUIRE(output_exists("tags/css/index.html"));
    std::string css = read_output("tags/css/index.html");
    REQUIRE(css.find("css") != std::string::npos);
    REQUIRE(css.find("Post A") != std::string::npos);
    REQUIRE(css.find("Post C") != std::string::npos);

    // /tags/rust/ should only list Post B
    REQUIRE(output_exists("tags/rust/index.html"));
    std::string rust = read_output("tags/rust/index.html");
    REQUIRE(rust.find("Post B") != std::string::npos);

    // /tags/design/ should only list Post C
    REQUIRE(output_exists("tags/design/index.html"));
    std::string design = read_output("tags/design/index.html");
    REQUIRE(design.find("Post C") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Integration: taxonomy with no tagged pages", "[integration][taxonomy]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");
    write_template("tag.html",
        "<html><body>{{ taxonomy.term }}</body></html>");
    write_template("tags.html",
        "<html><body></body></html>");

    auto cfg = make_config();
    Config::Taxonomy tax;
    tax.key = "tags";
    tax.template_ = "tag";
    tax.index_template = "tags";
    cfg.taxonomies.push_back(tax);

    auto result = build_site(cfg, true);

    // No tagged pages → no taxonomy pages generated
    REQUIRE_FALSE(output_exists("tags/index.html"));
}

TEST_CASE_METHOD(BuildFixture, "Integration: custom taxonomy field", "[integration][taxonomy]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_source("posts/a.md",
        "---\ntitle: Post A\ndate: 2024-01-01\ncategories: tutorials\n---\nPost A.\n");
    write_source("posts/b.md",
        "---\ntitle: Post B\ndate: 2024-02-01\ncategories: tutorials\n---\nPost B.\n");
    write_source("posts/c.md",
        "---\ntitle: Post C\ndate: 2024-03-01\ncategories: guides\n---\nPost C.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");
    write_template("category.html",
        "<html><body>"
        "<h1>{{ taxonomy.term }}</h1>"
        "{% for p in taxonomy.pages %}{{ p.title }}{% endfor %}"
        "</body></html>");
    write_template("categories.html",
        "<html><body>"
        "{% for t in taxonomy.terms %}{{ t.term }}({{ t.count }}){% endfor %}"
        "</body></html>");

    auto cfg = make_config();
    Config::Taxonomy tax;
    tax.key = "categories";
    tax.template_ = "category";
    tax.index_template = "categories";
    cfg.taxonomies.push_back(tax);

    auto result = build_site(cfg, true);

    // /categories/ index page
    REQUIRE(output_exists("categories/index.html"));
    std::string index = read_output("categories/index.html");
    REQUIRE(index.find("tutorials(2)") != std::string::npos);
    REQUIRE(index.find("guides(1)") != std::string::npos);

    // /categories/tutorials/ term page
    REQUIRE(output_exists("categories/tutorials/index.html"));
    std::string tut = read_output("categories/tutorials/index.html");
    REQUIRE(tut.find("Post A") != std::string::npos);
    REQUIRE(tut.find("Post B") != std::string::npos);
}

// --- Alias / Redirect Tests ---

TEST_CASE_METHOD(BuildFixture, "Integration: aliases generate redirect pages", "[integration][alias]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_source("posts/hello.md",
        "---\ntitle: Hello\ndate: 2024-01-01\naliases: [/old-hello/, /another/old/path/]\n---\nHello world.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto result = build_site(make_config(), true);

    // Both aliases should produce redirect pages
    REQUIRE(output_exists("old-hello/index.html"));
    REQUIRE(output_exists("another/old/path/index.html"));

    // Redirect pages must contain meta refresh + target URL
    std::string redirect1 = read_output("old-hello/index.html");
    REQUIRE(redirect1.find("refresh") != std::string::npos);
    REQUIRE(redirect1.find("/posts/hello/") != std::string::npos);

    std::string redirect2 = read_output("another/old/path/index.html");
    REQUIRE(redirect2.find("refresh") != std::string::npos);
    REQUIRE(redirect2.find("/posts/hello/") != std::string::npos);

    // Aliases must not leak into the rendered page HTML
    std::string page_html = read_output("posts/hello/index.html");
    REQUIRE(page_html.find("old-hello") == std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Integration: aliases in sitemap", "[integration][alias]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_source("posts/hello.md",
        "---\ntitle: Hello\ndate: 2024-01-01\naliases: [/old-post/]\n---\nHello.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    build_site(make_config(), true);

    std::string sitemap = read_output("sitemap.xml");
    REQUIRE(sitemap.find("/old-post/") != std::string::npos);
}

// --- JSON Feed Tests ---

TEST_CASE_METHOD(BuildFixture, "Integration: JSON Feed structure", "[integration][json_feed]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome body.\n");
    write_source("posts/hello.md",
        "---\ntitle: Hello\ndate: 2024-01-15\n---\n# Heading\nHello **world**.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto cfg = make_config();
    cfg.module_json_feed = true;

    build_site(cfg, true);

    REQUIRE(output_exists("feed.json"));
    auto feed = nlohmann::json::parse(read_output("feed.json"));

    REQUIRE(feed.value("version", "") == "https://jsonfeed.org/version/1.1");
    REQUIRE(feed.value("title", "") == "Test Site");
    REQUIRE(feed.value("home_page_url", "") == "https://example.com");
    REQUIRE(feed.value("feed_url", "") == "https://example.com/feed.json");
    REQUIRE(feed.value("language", "") == "en");
    REQUIRE(feed["items"].is_array());
    REQUIRE(feed["items"].size() >= 1);

    // Most recent post first; "Hello" (2024-01-15) should be item[0].
    const auto& item = feed["items"][0];
    REQUIRE(item.value("title", "") == "Hello");
    REQUIRE(item.value("url", "") == "https://example.com/posts/hello/");
    REQUIRE(item.value("id", "") == "https://example.com/posts/hello/");
    REQUIRE(item.value("date_published", "") == "2024-01-15");
    // content_html carries the rendered markdown body (<h1>, <strong>).
    std::string html = item.value("content_html", "");
    REQUIRE(html.find("<h1") != std::string::npos);
    REQUIRE(html.find("<strong>") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Integration: JSON Feed honors rss_item_count", "[integration][json_feed]") {
    write_source("posts/a.md", "---\ntitle: A\ndate: 2024-01-01\n---\nA.\n");
    write_source("posts/b.md", "---\ntitle: B\ndate: 2024-02-01\n---\nB.\n");
    write_source("posts/c.md", "---\ntitle: C\ndate: 2024-03-01\n---\nC.\n");
    write_template("default.html", "<html><body>{{ page.content }}</body></html>");

    auto cfg = make_config();
    cfg.module_json_feed = true;
    cfg.rss_item_count = 2;

    build_site(cfg, true);

    auto feed = nlohmann::json::parse(read_output("feed.json"));
    REQUIRE(feed["items"].size() == 2);
    // Descending date order: C, B.
    REQUIRE(feed["items"][0].value("title", "") == "C");
    REQUIRE(feed["items"][1].value("title", "") == "B");
}

TEST_CASE_METHOD(BuildFixture, "Integration: JSON Feed disabled by default", "[integration][json_feed]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_template("default.html", "<html><body>{{ page.content }}</body></html>");

    build_site(make_config(), true);

    REQUIRE_FALSE(output_exists("feed.json"));
}

TEST_CASE_METHOD(BuildFixture, "Integration: JSON Feed respects custom output filename", "[integration][json_feed]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_template("default.html", "<html><body>{{ page.content }}</body></html>");

    auto cfg = make_config();
    cfg.module_json_feed = true;
    cfg.json_feed_output = "index.json";

    build_site(cfg, true);

    REQUIRE(output_exists("index.json"));
    REQUIRE_FALSE(output_exists("feed.json"));
    auto feed = nlohmann::json::parse(read_output("index.json"));
    REQUIRE(feed.value("feed_url", "") == "https://example.com/index.json");
}

// --- Error Reporting Tests ---

TEST_CASE_METHOD(BuildFixture, "Integration: template error collected, good page still builds", "[integration][errors]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_source("good.md", "---\ntitle: Good\n---\nGood content.\n");
    write_source("bad.md", "---\ntitle: Bad\nlayout: broken\n---\nBad content.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");
    write_template("broken.html",
        "<html><body>{{ undefined_var.nonexistent }}</body></html>");

    auto result = build_site(make_config(), true);

    REQUIRE_FALSE(result.errors.empty());
    bool has_template_error = false;
    for (const auto& err : result.errors) {
        if (err.type == BuildError::Type::Template) {
            has_template_error = true;
            REQUIRE(err.template_name == "broken");
            break;
        }
    }
    REQUIRE(has_template_error);

    // Good page should still build
    REQUIRE(output_exists("good/index.html"));
    // Bad page should be skipped
    REQUIRE_FALSE(output_exists("bad/index.html"));
}

TEST_CASE_METHOD(BuildFixture, "Integration: frontmatter error collected", "[integration][errors]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    // Invalid YAML: unclosed flow mapping
    write_source("broken.md", "---\ntitle: Test\ndata: {unclosed\n---\nContent.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto result = build_site(make_config(), true);

    REQUIRE_FALSE(result.errors.empty());
    bool has_fm_error = false;
    for (const auto& err : result.errors) {
        if (err.type == BuildError::Type::Frontmatter) {
            has_fm_error = true;
            REQUIRE_FALSE(err.source_file.empty());
            break;
        }
    }
    REQUIRE(has_fm_error);

    // Index page should still build
    REQUIRE(output_exists("index.html"));
}

TEST_CASE_METHOD(BuildFixture, "Integration: frontmatter error carries line and column", "[integration][errors]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    // Invalid YAML: unclosed flow mapping on the third line of the file.
    //   file line 1: ---
    //   file line 2: title: T
    //   file line 3: data: {bad
    write_source("broken.md", "---\ntitle: T\ndata: {bad\n---\nbody\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto result = build_site(make_config(), true);

    bool found = false;
    for (const auto& err : result.errors) {
        if (err.type == BuildError::Type::Frontmatter) {
            found = true;
            REQUIRE(err.line > 0);
            REQUIRE(err.column > 0);
        }
    }
    REQUIRE(found);
}

TEST_CASE_METHOD(BuildFixture, "Integration: multiple errors collected", "[integration][errors]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_source("bad1.md", "---\ntitle: Bad1\nlayout: broken1\n---\nBad1.\n");
    write_source("bad2.md", "---\ntitle: Bad2\nlayout: broken2\n---\nBad2.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");
    write_template("broken1.html",
        "<html><body>{{ undefined1.nonexistent }}</body></html>");
    write_template("broken2.html",
        "<html><body>{{ undefined2.nonexistent }}</body></html>");

    auto result = build_site(make_config(), true);

    int template_errors = 0;
    for (const auto& err : result.errors) {
        if (err.type == BuildError::Type::Template) template_errors++;
    }
    REQUIRE(template_errors >= 2);

    // Good page should still build
    REQUIRE(output_exists("index.html"));
}

// --- Hook Tests ---

TEST_CASE_METHOD(BuildFixture, "Integration: before_build hook runs", "[integration][hooks]") {
    std::string marker_path = root_dir + "/marker.txt";
    std::string script = write_hook("pre-build.sh",
        "touch \"" + marker_path + "\"\n");

    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto cfg = make_config();
    cfg.hook_before_build = script;

    auto result = build_site(cfg, true);
    REQUIRE(result.errors.empty());
    REQUIRE(fs::exists(marker_path));
    REQUIRE(output_exists("index.html"));
}

TEST_CASE_METHOD(BuildFixture, "Integration: after_build hook receives CSTATIC_PAGES_BUILT", "[integration][hooks]") {
    std::string output_file = root_dir + "/pages-built.txt";
    std::string script = write_hook("post-build.sh",
        "echo $CSTATIC_PAGES_BUILT > \"" + output_file + "\"\n");

    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_source("about.md", "---\ntitle: About\n---\nAbout.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto cfg = make_config();
    cfg.hook_after_build = script;

    auto result = build_site(cfg, true);
    REQUIRE(result.errors.empty());
    REQUIRE(fs::exists(output_file));

    std::ifstream f(output_file);
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    // Should have built at least 2 pages
    int built = std::atoi(content.c_str());
    REQUIRE(built >= 2);
}

TEST_CASE_METHOD(BuildFixture, "Integration: hook failure aborts build", "[integration][hooks]") {
    std::string script = write_hook("failing.sh", "exit 1\n");

    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto cfg = make_config();
    cfg.hook_before_build = script;

    auto result = build_site(cfg, true);
    REQUIRE_FALSE(result.errors.empty());
    REQUIRE(result.errors[0].message.find("before_build") != std::string::npos);
    // Build should be aborted
    REQUIRE_FALSE(output_exists("index.html"));
}

TEST_CASE_METHOD(BuildFixture, "Integration: missing hook warns but continues", "[integration][hooks]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto cfg = make_config();
    cfg.hook_before_build = root_dir + "/nonexistent.sh";

    auto result = build_site(cfg, true);
    REQUIRE(result.errors.empty());
    REQUIRE(output_exists("index.html"));
}

TEST_CASE_METHOD(BuildFixture, "SEO: generates og and twitter meta tags", "[integration][seo]") {
    write_source("index.md",
        "---\n"
        "title: SEO Page\n"
        "description: A page about SEO\n"
        "image: /images/og.png\n"
        "---\n# Hello\nContent here.\n");
    write_template("default.html",
        "<html><head><title>{{ page.title }}</title>{{ seo_meta }}</head>"
        "<body>{{ page.content }}</body></html>");

    auto cfg = make_config();
    cfg.site_twitter_handle = "@testsite";
    cfg.minify_html = false;

    auto result = build_site(cfg, true);
    REQUIRE(result.errors.empty());
    REQUIRE(output_exists("index.html"));

    std::string html = read_output("index.html");
    REQUIRE(html.find("<meta property=\"og:title\"") != std::string::npos);
    REQUIRE(html.find("twitter:card") != std::string::npos);
    REQUIRE(html.find("<link rel=\"canonical\"") != std::string::npos);
    REQUIRE(html.find("summary_large_image") != std::string::npos);
    REQUIRE(html.find("@testsite") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Search: generates search-index.json with pages", "[integration][search]") {
    write_source("index.md", "---\ntitle: Home\n---\nWelcome.\n");
    write_source("about.md", "---\ntitle: About\n---\nAbout us.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto cfg = make_config();
    cfg.search_enabled = true;

    auto result = build_site(cfg, true);
    REQUIRE(result.errors.empty());
    REQUIRE(output_exists("search-index.json"));

    std::string json = read_output("search-index.json");
    REQUIRE(json.find("Home") != std::string::npos);
    REQUIRE(json.find("/about/") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Sitemap: includes changefreq and priority", "[integration][sitemap][seo]") {
    write_source("index.md",
        "---\n"
        "title: Home\n"
        "sitemap_changefreq: monthly\n"
        "sitemap_priority: 0.8\n"
        "---\nContent.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto cfg = make_config();

    auto result = build_site(cfg, true);
    REQUIRE(result.errors.empty());

    std::string xml = read_output("sitemap.xml");
    REQUIRE(xml.find("<changefreq>monthly</changefreq>") != std::string::npos);
    REQUIRE(xml.find("<priority>0.8</priority>") != std::string::npos);
}

// --- OG Image Tests ---

TEST_CASE_METHOD(BuildFixture, "OG Images: generates SVG and injects og:image meta", "[integration][og]") {
    write_source("index.md", "---\ntitle: Home Page\n---\nHome.\n");
    write_source("posts/hello.md", "---\ntitle: Hello Post\ndate: 2024-01-01\n---\nHello.\n");
    write_template("default.html",
        "<html><head><title>{{ page.title }}</title>{{ seo_meta }}</head>"
        "<body>{{ page.content }}</body></html>");
    write_template("og-default.svg",
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1200\" height=\"630\">"
        "<text x=\"10\" y=\"100\">{{ page.title }}</text>"
        "<text x=\"10\" y=\"200\">{{ site.title }}</text>"
        "</svg>");

    auto cfg = make_config();
    cfg.og_images_enabled = true;
    cfg.og_images_output_format = "svg";  // avoid converter dependency
    cfg.minify_html = false;

    auto result = build_site(cfg, true);
    REQUIRE(result.errors.empty());

    // SVG files generated for titled pages
    REQUIRE(output_exists("og/index.svg"));           // "/" -> slug "index"
    REQUIRE(output_exists("og/posts-hello.svg"));     // "/posts/hello/" -> slug "posts-hello"

    // SVG content rendered with page context
    std::string home_svg = read_output("og/index.svg");
    REQUIRE(home_svg.find("Home Page") != std::string::npos);
    REQUIRE(home_svg.find("Test Site") != std::string::npos);

    // og:image meta injected into rendered HTML (proves og_image propagated via pages_array)
    std::string html = read_output("index.html");
    REQUIRE(html.find("og:image") != std::string::npos);
    REQUIRE(html.find("/og/index.svg") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "OG Images: module skips pages without titles", "[integration][og]") {
    write_template("og-default.svg",
        "<svg xmlns=\"http://www.w3.org/2000/svg\"><text>{{ page.title }}</text></svg>");

    // Markdown titles always fall back to the filename stem, so exercise the skip
    // logic directly against the module with an explicitly untitled page.
    nlohmann::json pages = nlohmann::json::array();
    pages.push_back(nlohmann::json{{"title", "Titled"}, {"url", "/titled/"}});
    pages.push_back(nlohmann::json{{"url", "/untitled/"}});  // no title field

    Config cfg = make_config();
    cfg.og_images_enabled = true;
    cfg.og_images_output_format = "svg";

    int count = cstatic::modules::generate_og_images(cfg, pages,
        root_dir + "/output", root_dir + "/templates");
    REQUIRE(count == 1);

    // Titled page gets an image + og_image URL; untitled page is skipped
    REQUIRE(output_exists("og/titled.svg"));
    REQUIRE_FALSE(output_exists("og/untitled.svg"));
    REQUIRE(pages[0].value("og_image", "") == "/og/titled.svg");
    REQUIRE(pages[1].value("og_image", "") == "");
}

TEST_CASE_METHOD(BuildFixture, "OG Images: disabled by default produces no og dir", "[integration][og]") {
    write_source("index.md", "---\ntitle: Home\n---\nHome.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}</body></html>");

    auto cfg = make_config();
    // og_images_enabled defaults to false
    build_site(cfg, true);

    REQUIRE_FALSE(output_exists("og/index.svg"));
    REQUIRE_FALSE(fs::exists(root_dir + "/output/og"));
}

// ---------------------------------------------------------------------------
// Wikilinks + backlinks (Feature #6)
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(BuildFixture, "Wikilinks: resolves and emits backlinks", "[integration][wikilinks]") {
    // a.md wikilinks to b.md via [[b]]; b.md is titled "B Page".
    write_source("a.md", "---\ntitle: Alpha\n---\nLink: [[b]].\n");
    write_source("b.md", "---\ntitle: B Page\n---\nBeta body.\n");
    write_template("default.html",
        "<html><body>"
        "{{ page.content }}"
        "<section id=\"backlinks\">"
        "{% for bl in page.backlinks %}<span class=\"bl\">{{ bl.title }}</span>{% endfor %}"
        "</section>"
        "</body></html>");

    auto cfg = make_config();
    cfg.wikilinks_enabled = true;
    cfg.minify_html = false;  // keep attribute quotes stable for assertions
    auto result = build_site(cfg, /*incremental=*/false);
    REQUIRE(result.pages_built >= 2);

    // a's body should contain the resolved wikilink (display defaults to target).
    std::string a_html = read_output("a/index.html");
    REQUIRE(a_html.find("<a href=\"/b/\">b</a>") != std::string::npos);

    // b's backlinks section should mention Alpha (a links to b).
    std::string b_html = read_output("b/index.html");
    REQUIRE(b_html.find("class=\"bl\"") != std::string::npos);
    REQUIRE(b_html.find("Alpha") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Wikilinks: disabled by default leaves syntax intact", "[integration][wikilinks]") {
    write_source("a.md", "---\ntitle: Alpha\n---\nLink: [[b]].\n");
    write_source("b.md", "---\ntitle: Beta\n---\nBeta.\n");
    write_template("default.html", "<html><body>{{ page.content }}</body></html>");

    auto cfg = make_config();
    // wikilinks_enabled defaults to false
    build_site(cfg, /*incremental=*/false);

    std::string a_html = read_output("a/index.html");
    // With wikilinks off, the brackets survive as literal text (cmark may
    // wrap them in a <code>-like element, but the raw `[[b]]` chars persist).
    REQUIRE(a_html.find("[[b]]") != std::string::npos);
    // And no wikilink anchor was emitted.
    REQUIRE(a_html.find("href=\"/b/\">b</a>") == std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Wikilinks: title change invalidates linker incrementally", "[integration][wikilinks]") {
    write_source("a.md", "---\ntitle: Alpha\n---\nLink: [[b]].\n");
    write_source("b.md", "---\ntitle: Beta\n---\nBeta body.\n");
    write_template("default.html",
        "<html><body>"
        "{{ page.content }}"
        "<section>{% for bl in page.backlinks %}<span>{{ bl.title }}</span>{% endfor %}</section>"
        "</body></html>");

    auto cfg = make_config();
    cfg.wikilinks_enabled = true;

    // First build — both pages built.
    auto r1 = build_site(cfg, /*incremental=*/true);
    REQUIRE(r1.pages_built >= 2);
    // b's backlinks mention Alpha.
    REQUIRE(read_output("b/index.html").find("Alpha") != std::string::npos);

    // Touch only b.md's title. a.md's hash is unchanged, but the wikilinks
    // index hash changes — so a should also rebuild on the next pass.
    write_source("b.md", "---\ntitle: Beta Renamed\n---\nBeta body.\n");
    auto r2 = build_site(cfg, /*incremental=*/true);
    // At least 2 pages built (b.md changed + a.md invalidated via wikilinks).
    REQUIRE(r2.pages_built >= 2);
}

TEST_CASE_METHOD(BuildFixture, "Incremental: adding a page updates {{ pages }} lists", "[integration][incremental]") {
    // a.md renders a template that lists every page title via {{ pages }}.
    // wikilinks are OFF so the only invalidation signal is meta:pages_array.
    write_source("a.md", "---\ntitle: Alpha\n---\nBody.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}"
        "{% for p in pages %}<span>{{ p.title }}</span>{% endfor %}"
        "</body></html>");

    auto cfg = make_config();
    cfg.wikilinks_enabled = false;

    // First build — a lists only Alpha.
    auto r1 = build_site(cfg, true);
    REQUIRE(r1.pages_built >= 1);
    REQUIRE(read_output("a/index.html").find("Alpha") != std::string::npos);
    REQUIRE(read_output("a/index.html").find("Beta") == std::string::npos);

    // Add b.md. a.md source is unchanged, BUT pages_array changes — so a
    // must rebuild on the next incremental pass to pick up Beta.
    write_source("b.md", "---\ntitle: Beta\n---\nBody.\n");
    auto r2 = build_site(cfg, false);
    // Both pages should report built (b is new, a is invalidated).
    REQUIRE(r2.pages_built >= 2);
    REQUIRE(read_output("a/index.html").find("Beta") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "Incremental: deleting a page updates {{ pages }} lists", "[integration][incremental]") {
    write_source("a.md", "---\ntitle: Alpha\n---\nBody.\n");
    write_source("b.md", "---\ntitle: Beta\n---\nBody.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}"
        "{% for p in pages %}<span>{{ p.title }}</span>{% endfor %}"
        "</body></html>");

    auto cfg = make_config();
    cfg.wikilinks_enabled = false;

    auto r1 = build_site(cfg, true);
    REQUIRE(r1.pages_built >= 2);
    REQUIRE(read_output("a/index.html").find("Beta") != std::string::npos);

    // Delete b.md. a.md source unchanged, but pages_array changes — so a
    // rebuilds and no longer mentions Beta.
    fs::remove(root_dir + "/src/b.md");
    auto r2 = build_site(cfg, false);
    REQUIRE(read_output("a/index.html").find("Beta") == std::string::npos);
    // b's output should be cleaned up as an orphan.
    REQUIRE_FALSE(output_exists("b/index.html"));
}

TEST_CASE_METHOD(BuildFixture, "Incremental: title change on one page invalidates siblings via {{ pages }}", "[integration][incremental]") {
    write_source("a.md", "---\ntitle: Alpha\n---\nBody.\n");
    write_source("b.md", "---\ntitle: Beta\n---\nBody.\n");
    write_template("default.html",
        "<html><body>{{ page.content }}"
        "{% for p in pages %}<span>{{ p.title }}</span>{% endfor %}"
        "</body></html>");

    auto cfg = make_config();
    cfg.wikilinks_enabled = false;

    auto r1 = build_site(cfg, true);
    REQUIRE(r1.pages_built >= 2);

    // Change only b.md's title. a.md source is unchanged, but pages_array
    // changes — so a rebuilds to show "Beta Renamed" in its pages list.
    write_source("b.md", "---\ntitle: Beta Renamed\n---\nBody.\n");
    auto r2 = build_site(cfg, false);
    // Both pages report built: b (source changed) + a (invalidated via pages_array).
    REQUIRE(r2.pages_built >= 2);
    REQUIRE(read_output("a/index.html").find("Beta Renamed") != std::string::npos);
}

TEST_CASE_METHOD(BuildFixture, "G15: per-page markdown mirror", "[integration][mirror]") {
    write_template("default.html",
        "<html><head>{{ seo_meta }}</head><body>{{ page.content }}</body></html>");

    SECTION("frontmatter mirror_markdown emits .md + alternate link") {
        write_source("posts/hello.md",
            "---\ntitle: Hello\nmirror_markdown: true\n---\n# Hello\n\nWorld paragraph.\n");
        auto cfg = make_config();
        cfg.markdown_mirror_enabled = true;
        cfg.minify_html = false;

        auto r = build_site(cfg, true);
        REQUIRE(r.pages_built >= 1);
        REQUIRE(output_exists("posts/hello/index.html"));
        REQUIRE(output_exists("posts/hello/index.md"));

        // Alternate link advertises the mirror (absolute, with base_url).
        std::string html = read_output("posts/hello/index.html");
        REQUIRE(html.find("<link rel=\"alternate\" type=\"text/markdown\""
                          " href=\"https://example.com/posts/hello/index.md\">")
                != std::string::npos);

        // Mirror body is the processed markdown — NOT HTML-rendered: the "#"
        // heading marker survives, and there is no <h1> tag.
        std::string md = read_output("posts/hello/index.md");
        REQUIRE(md.find("# Hello") != std::string::npos);
        REQUIRE(md.find("<h1") == std::string::npos);
        REQUIRE(md.find("World paragraph.") != std::string::npos);
    }

    SECTION("mirror_all mirrors every page regardless of frontmatter") {
        write_source("posts/a.md", "---\ntitle: A\n---\nA body.\n");
        write_source("about.md", "---\ntitle: About\n---\nAbout body.\n");
        auto cfg = make_config();
        cfg.markdown_mirror_enabled = true;
        cfg.markdown_mirror_all = true;
        cfg.minify_html = false;

        build_site(cfg, true);
        REQUIRE(output_exists("posts/a/index.md"));
        REQUIRE(output_exists("about/index.md"));

        // Both pages advertise their mirrors.
        REQUIRE(read_output("posts/a/index.html").find("text/markdown")
                != std::string::npos);
        REQUIRE(read_output("about/index.html").find("text/markdown")
                != std::string::npos);
    }

    SECTION("disabled by default: no mirror files or links") {
        write_source("posts/hello.md",
            "---\ntitle: Hello\nmirror_markdown: true\n---\n# Hello\n");
        auto cfg = make_config();  // mirror not enabled
        cfg.minify_html = false;

        build_site(cfg, true);
        REQUIRE_FALSE(output_exists("posts/hello/index.md"));
        REQUIRE(read_output("posts/hello/index.html").find("text/markdown")
                == std::string::npos);
    }

    SECTION("enabled but page not opted in: no mirror for that page") {
        write_source("posts/opted.md",
            "---\ntitle: Opted\nmirror_markdown: true\n---\nBody.\n");
        write_source("posts/plain.md", "---\ntitle: Plain\n---\nBody.\n");
        auto cfg = make_config();
        cfg.markdown_mirror_enabled = true;  // all = false
        cfg.minify_html = false;

        build_site(cfg, true);
        REQUIRE(output_exists("posts/opted/index.md"));
        REQUIRE_FALSE(output_exists("posts/plain/index.md"));
        // Only the opted-in page carries the alternate link.
        REQUIRE(read_output("posts/opted/index.html").find("text/markdown")
                != std::string::npos);
        REQUIRE(read_output("posts/plain/index.html").find("text/markdown")
                == std::string::npos);
    }

    SECTION("custom suffix is honored") {
        write_source("posts/hello.md",
            "---\ntitle: Hello\nmirror_markdown: true\n---\n# Hello\n");
        auto cfg = make_config();
        cfg.markdown_mirror_enabled = true;
        cfg.markdown_mirror_suffix = ".markdown";
        cfg.minify_html = false;

        build_site(cfg, true);
        REQUIRE(output_exists("posts/hello/index.markdown"));
        REQUIRE_FALSE(output_exists("posts/hello/index.md"));
        REQUIRE(read_output("posts/hello/index.html").find(
                    "href=\"https://example.com/posts/hello/index.markdown\">")
                != std::string::npos);
    }

    SECTION("incremental build preserves mirror files (no asset-orphan removal)") {
        write_source("posts/hello.md",
            "---\ntitle: Hello\nmirror_markdown: true\n---\n# Hello\n");
        auto cfg = make_config();
        cfg.markdown_mirror_enabled = true;
        cfg.minify_html = false;

        build_site(cfg, true);
        REQUIRE(output_exists("posts/hello/index.md"));

        // Second build is incremental (no source changes) — the page is
        // served from cache. The mirror file must survive: neither the HTML
        // orphan cleanup nor the asset-pipeline orphan cleanup may remove it.
        auto r2 = build_site(cfg, false);
        REQUIRE(r2.pages_cached >= 1);
        REQUIRE(output_exists("posts/hello/index.md"));
        std::string md = read_output("posts/hello/index.md");
        REQUIRE(md.find("# Hello") != std::string::npos);
    }
}
