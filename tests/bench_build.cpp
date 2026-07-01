#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <filesystem>
#include <fstream>
#include <cstdlib>

#include "config/config.hpp"
#include "pipeline/builder.hpp"
#include "content/frontmatter.hpp"
#include "content/markdown.hpp"
#include "assets/asset_pipeline.hpp"
#include "utils/file_io.hpp"
#include "test_util.hpp"

namespace fs = std::filesystem;

using namespace cstatic;

struct BenchFixture {
    std::string root_dir;
    std::string saved_cwd;

    BenchFixture() {
        saved_cwd = fs::current_path().string();

        root_dir = cstatic_test::unique_temp_dir("cstatic_bench_");
        fs::create_directories(root_dir);
        fs::create_directories(root_dir + "/src");
        fs::create_directories(root_dir + "/templates");
        fs::create_directories(root_dir + "/static");
        fs::create_directories(root_dir + "/_data");

        cstatic::utils::write_file(root_dir + "/config.toml",
            "[site]\ntitle = \"Bench Site\"\nbase_url = \"https://example.com\"\n");

        fs::current_path(root_dir);
    }

    ~BenchFixture() {
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

    Config make_config() const {
        Config cfg;
        cfg.site_title = "Bench Site";
        cfg.site_base_url = "https://example.com";
        cfg.source_dir = root_dir + "/src";
        cfg.output_dir = root_dir + "/output";
        cfg.template_dir = root_dir + "/templates";
        cfg.static_dir = root_dir + "/static";
        cfg.data_dir = root_dir + "/_data";
        cfg.incremental_enabled = false;
        return cfg;
    }
};

// Generate a large CSS string for benchmarking
static std::string make_large_css(int rules) {
    std::string css;
    css.reserve(rules * 80);
    for (int i = 0; i < rules; i++) {
        css += ".class-" + std::to_string(i) + " {\n";
        css += "  color: #" + std::to_string(i % 16) + std::to_string((i * 3) % 16)
             + std::to_string((i * 7) % 16) + "a" + std::to_string((i * 2) % 16) + "f"
             + std::to_string((i * 5) % 16) + ";\n";
        css += "  background-color: #ffffff;\n";
        css += "  margin: 0 auto;\n";
        css += "  padding: 10px 20px;\n";
        css += "}\n";
    }
    return css;
}

// Generate a large JS string for benchmarking
static std::string make_large_js(int lines) {
    std::string js;
    js.reserve(lines * 60);
    for (int i = 0; i < lines; i++) {
        js += "const variable" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
        js += "function func" + std::to_string(i) + "() { return variable" + std::to_string(i) + " + 1; }\n";
    }
    return js;
}

// Generate a long markdown document for benchmarking
static std::string make_long_markdown(int paragraphs) {
    std::string md;
    md.reserve(paragraphs * 200);
    for (int i = 0; i < paragraphs; i++) {
        md += "## Section " + std::to_string(i) + "\n\n";
        md += "This is paragraph " + std::to_string(i) + " with some **bold text** and "
             + "*italic text* and a [link](https://example.com).\n\n";
        md += "- List item one\n- List item two\n- List item three\n\n";
    }
    return md;
}

// Generate frontmatter with many fields
static std::string make_complex_frontmatter(int fields) {
    std::string fm = "---\ntitle: Complex Page\ndate: 2024-01-15\n";
    for (int i = 0; i < fields; i++) {
        fm += "custom_field_" + std::to_string(i) + ": value_" + std::to_string(i) + "\n";
    }
    fm += "---\nBody content here.\n";
    return fm;
}

TEST_CASE_METHOD(BenchFixture, "Benchmark: full build with 100 pages", "[benchmark]") {
    write_template("default.html",
        "<html><head><title>{{ page.title }}</title></head>"
        "<body>{{ page.content }}</body></html>");

    for (int i = 0; i < 100; i++) {
        write_source("posts/post-" + std::to_string(i) + ".md",
            "---\ntitle: Post " + std::to_string(i) + "\ndate: 2024-01-" +
            (i < 10 ? "0" : "") + std::to_string(i % 28 + 1) + "\n---\n" +
            make_long_markdown(3));
    }

    auto cfg = make_config();

    BENCHMARK("build 100 pages") {
        return build_site(cfg, true);
    };
}

TEST_CASE("Benchmark: CSS minification", "[benchmark]") {
    std::string large_css = make_large_css(500);

    BENCHMARK("minify 500 CSS rules") {
        return minify_css(large_css);
    };
}

TEST_CASE("Benchmark: JS minification", "[benchmark]") {
    std::string large_js = make_large_js(500);

    BENCHMARK("minify 500 JS functions") {
        return minify_js(large_js);
    };
}

TEST_CASE("Benchmark: Markdown rendering", "[benchmark]") {
    std::string long_md = make_long_markdown(100);

    BENCHMARK("render 100 markdown paragraphs") {
        return render_markdown(long_md);
    };
}

TEST_CASE("Benchmark: Frontmatter parsing", "[benchmark]") {
    std::string complex_fm = make_complex_frontmatter(50);

    BENCHMARK("parse frontmatter with 50 custom fields") {
        return parse_frontmatter(complex_fm, "test.md");
    };
}
