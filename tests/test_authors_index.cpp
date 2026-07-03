#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "content/authors_index.hpp"
#include "utils/file_io.hpp"
#include "test_util.hpp"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using cstatic::AuthorsIndex;
using cstatic::utils::write_file;

namespace {

// RAII temp dir that mimics a project's src/authors/ directory.
struct AuthorsDir {
    fs::path dir;
    AuthorsDir() : dir(cstatic_test::unique_temp_dir("cstatic_authors_")) {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~AuthorsDir() { fs::remove_all(dir); }
    std::string path() const { return dir.string(); }
    void write(const std::string& slug, const std::string& content) {
        write_file((dir / (slug + ".md")).string(), content);
    }
};

} // anonymous namespace

TEST_CASE("authors: load resolves frontmatter fields", "[authors]") {
    AuthorsDir d;
    d.write("jane-doe",
        "---\n"
        "name: \"Jane Doe\"\n"
        "title: \"Founder\"\n"
        "bio: \"Writes about static sites.\"\n"
        "github: \"janedoe\"\n"
        "website: \"https://example.com\"\n"
        "same_as:\n"
        "  - \"https://github.com/janedoe\"\n"
        "expertise:\n"
        "  - \"C++\"\n"
        "  - \"SSGs\"\n"
        "---\n"
        "Bio body text.\n");

    AuthorsIndex idx;
    idx.load(d.path());

    REQUIRE(idx.has("jane-doe"));
    REQUIRE_FALSE(idx.has("nobody"));

    auto ctx = idx.context("jane-doe");
    REQUIRE(ctx["slug"] == "jane-doe");
    REQUIRE(ctx["name"] == "Jane Doe");
    REQUIRE(ctx["title"] == "Founder");
    REQUIRE(ctx["bio"] == "Writes about static sites.");
    REQUIRE(ctx["github"] == "janedoe");
    REQUIRE(ctx["website"] == "https://example.com");
    REQUIRE(ctx["same_as"].size() == 1);
    REQUIRE(ctx["same_as"][0] == "https://github.com/janedoe");
    REQUIRE(ctx["expertise"].size() == 2);
}

TEST_CASE("authors: name falls back to slug when frontmatter omits it", "[authors]") {
    AuthorsDir d;
    d.write("anon", "---\nbio: \"Shy.\"\n---\nBody.\n");

    AuthorsIndex idx;
    idx.load(d.path());

    REQUIRE(idx.has("anon"));
    auto ctx = idx.context("anon");
    REQUIRE(ctx["name"] == "anon");
    REQUIRE(ctx["bio"] == "Shy.");
}

TEST_CASE("authors: unknown slug returns null context and schema", "[authors]") {
    AuthorsDir d;
    d.write("real", "---\nname: \"Real\"\n---\n.\n");

    AuthorsIndex idx;
    idx.load(d.path());

    REQUIRE(idx.context("missing").is_null());
    REQUIRE(idx.person_schema("missing", "https://x/y/").is_null());
}

TEST_CASE("authors: person_schema shape", "[authors]") {
    AuthorsDir d;
    d.write("jane",
        "---\n"
        "name: \"Jane\"\n"
        "title: \"Editor\"\n"
        "email: \"jane@example.com\"\n"
        "avatar: \"/img/jane.png\"\n"
        "same_as:\n"
        "  - \"https://github.com/jane\"\n"
        "---\n.\n");

    AuthorsIndex idx;
    idx.load(d.path());

    auto p = idx.person_schema("jane", "https://example.com/authors/jane/");
    REQUIRE(p["@type"] == "Person");
    REQUIRE(p["name"] == "Jane");
    REQUIRE(p["url"] == "https://example.com/authors/jane/");
    REQUIRE(p["jobTitle"] == "Editor");
    REQUIRE(p["email"] == "jane@example.com");
    REQUIRE(p["image"] == "/img/jane.png");
    REQUIRE(p["sameAs"].size() == 1);
    REQUIRE(p["sameAs"][0] == "https://github.com/jane");
}

TEST_CASE("authors: all_slugs sorted and size/empty", "[authors]") {
    AuthorsDir d;
    d.write("zoe", "---\nname: \"Zoe\"\n---\n.\n");
    d.write("amy", "---\nname: \"Amy\"\n---\n.\n");

    AuthorsIndex idx;
    idx.load(d.path());

    auto slugs = idx.all_slugs();
    REQUIRE(slugs.size() == 2);
    REQUIRE(slugs[0] == "amy");
    REQUIRE(slugs[1] == "zoe");
    REQUIRE(idx.size() == 2);
    REQUIRE_FALSE(idx.empty());
}

TEST_CASE("authors: missing directory leaves index empty", "[authors]") {
    AuthorsIndex idx;
    idx.load("/no/such/dir/authors");
    REQUIRE(idx.empty());
    REQUIRE(idx.all_slugs().empty());
}

TEST_CASE("authors: only .md files loaded", "[authors]") {
    AuthorsDir d;
    d.write("real", "---\nname: \"Real\"\n---\n.\n");
    // A non-markdown file should be ignored.
    write_file((d.dir / "readme.txt").string(), "not an author\n");

    AuthorsIndex idx;
    idx.load(d.path());
    REQUIRE(idx.size() == 1);
    REQUIRE(idx.has("real"));
    REQUIRE_FALSE(idx.has("readme"));
}

TEST_CASE("authors: context omits empty optional fields", "[authors]") {
    AuthorsDir d;
    d.write("min", "---\nname: \"Min\"\n---\n.\n");

    AuthorsIndex idx;
    idx.load(d.path());
    auto ctx = idx.context("min");
    REQUIRE(ctx["name"] == "Min");
    REQUIRE_FALSE(ctx.contains("title"));
    REQUIRE_FALSE(ctx.contains("bio"));
    REQUIRE_FALSE(ctx.contains("same_as"));
}
