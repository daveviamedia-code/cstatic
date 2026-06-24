#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "cli/content_generator.hpp"

namespace fs = std::filesystem;

using namespace cstatic::cli;

namespace {

// Per-test temp project root containing src/ and archetypes/. Tests chdir
// into it so generate_content resolves relative paths correctly.
struct ContentFixture {
    std::string root_dir;
    std::string saved_cwd;

    ContentFixture() {
        saved_cwd = fs::current_path().string();
        root_dir = (fs::temp_directory_path() /
                   ("cstatic_cg_" + std::to_string(std::rand()))).string();
        fs::create_directories(root_dir + "/src");
        fs::create_directories(root_dir + "/archetypes");
        fs::current_path(root_dir);
    }

    ~ContentFixture() {
        fs::current_path(saved_cwd);
        fs::remove_all(root_dir);
    }

    void write_archetype(const std::string& name, const std::string& content) {
        std::string path = root_dir + "/archetypes/" + name + ".md";
        std::ofstream f(path);
        f << content;
        f.close();
        REQUIRE(f.good());
    }

    static std::string read(const std::string& path) {
        std::ifstream f(path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
};

} // anonymous namespace

TEST_CASE("Content generator: built-in default archetype", "[content_generator]") {
    ContentFixture fx;
    // No archetype files written — built-in default should be used.
    int rc = generate_content("src/hello-world.md", "archetypes", "", "2025-06-17");
    REQUIRE(rc == 0);
    REQUIRE(fs::exists("src/hello-world.md"));
    std::string body = ContentFixture::read("src/hello-world.md");
    REQUIRE(body.find("title: \"Hello World\"") != std::string::npos);
    REQUIRE(body.find("date: \"2025-06-17\"") != std::string::npos);
    REQUIRE(body.find("# Hello World") != std::string::npos);
}

TEST_CASE("Content generator: custom default.md archetype", "[content_generator]") {
    ContentFixture fx;
    fx.write_archetype("default", "---\ntitle: \"{{ title }}\"\ndate: \"{{ date }}\"\nslug: \"{{ slug }}\"\n---\n\nBody for {{ title }}.\n");
    int rc = generate_content("src/my-cool-post.md", "archetypes", "", "2025-01-02");
    REQUIRE(rc == 0);
    std::string body = ContentFixture::read("src/my-cool-post.md");
    REQUIRE(body.find("title: \"My Cool Post\"") != std::string::npos);
    REQUIRE(body.find("date: \"2025-01-02\"") != std::string::npos);
    REQUIRE(body.find("slug: \"my-cool-post\"") != std::string::npos);
    REQUIRE(body.find("Body for My Cool Post.") != std::string::npos);
}

TEST_CASE("Content generator: custom kind", "[content_generator]") {
    ContentFixture fx;
    fx.write_archetype("post", "---\ntitle: \"{{ title }}\"\ndraft: true\n---\n\n# {{ title }}\n");
    int rc = generate_content("src/posts/launch.md", "archetypes", "post", "2025-03-04");
    REQUIRE(rc == 0);
    REQUIRE(fs::exists("src/posts/launch.md"));
    std::string body = ContentFixture::read("src/posts/launch.md");
    REQUIRE(body.find("title: \"Launch\"") != std::string::npos);
    REQUIRE(body.find("draft: true") != std::string::npos);
}

TEST_CASE("Content generator: title derivation", "[content_generator]") {
    ContentFixture fx;
    // Hyphens, underscores, and mixed casing.
    generate_content("src/my-cool_post.md", "archetypes", "", "2025-01-01");
    std::string body = ContentFixture::read("src/my-cool_post.md");
    REQUIRE(body.find("title: \"My Cool Post\"") != std::string::npos);

    // Single word.
    generate_content("src/about.md", "archetypes", "", "2025-01-01");
    REQUIRE(ContentFixture::read("src/about.md").find("title: \"About\"") != std::string::npos);
}

TEST_CASE("Content generator: overwrite protection", "[content_generator]") {
    ContentFixture fx;
    // Pre-create the target file.
    {
        std::ofstream f("src/existing.md");
        f << "ORIGINAL CONTENT\n";
        f.close();
    }
    int rc = generate_content("src/existing.md", "archetypes", "", "2025-01-01");
    REQUIRE(rc != 0);
    // File must be untouched.
    REQUIRE(ContentFixture::read("src/existing.md") == "ORIGINAL CONTENT\n");
}

TEST_CASE("Content generator: creates parent directories", "[content_generator]") {
    ContentFixture fx;
    int rc = generate_content("src/nested/deep/sub/page.md", "archetypes", "", "2025-01-01");
    REQUIRE(rc == 0);
    REQUIRE(fs::exists("src/nested/deep/sub/page.md"));
    std::string body = ContentFixture::read("src/nested/deep/sub/page.md");
    REQUIRE(body.find("title: \"Page\"") != std::string::npos);
}

TEST_CASE("Content generator: unknown kind falls back to default.md", "[content_generator]") {
    ContentFixture fx;
    fx.write_archetype("default", "---\ntitle: \"{{ title }}\"\n---\nDEFAULT BODY\n");
    // No 'newsletter.md' exists — should warn + use default.md.
    int rc = generate_content("src/issue-1.md", "archetypes", "newsletter", "2025-01-01");
    REQUIRE(rc == 0);
    std::string body = ContentFixture::read("src/issue-1.md");
    REQUIRE(body.find("DEFAULT BODY") != std::string::npos);
    REQUIRE(body.find("title: \"Issue 1\"") != std::string::npos);
}

TEST_CASE("Content generator: placeholder tolerant of spacing", "[content_generator]") {
    ContentFixture fx;
    fx.write_archetype("default", "T:{{title}} D:{{ date }}\n");
    int rc = generate_content("src/x.md", "archetypes", "", "2025-09-09");
    REQUIRE(rc == 0);
    std::string body = ContentFixture::read("src/x.md");
    REQUIRE(body.find("T:X D:2025-09-09") != std::string::npos);
}
