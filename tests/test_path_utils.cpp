#include <catch2/catch_test_macros.hpp>
#include "utils/path.hpp"

using namespace cstatic::utils;

TEST_CASE("path_join: both empty", "[path]") {
    REQUIRE(path_join("", "") == "");
}

TEST_CASE("path_join: first empty", "[path]") {
    REQUIRE(path_join("", "foo") == "foo");
}

TEST_CASE("path_join: second empty", "[path]") {
    REQUIRE(path_join("foo", "") == "foo");
}

TEST_CASE("path_join: no trailing slash on first", "[path]") {
    REQUIRE(path_join("foo", "bar") == "foo/bar");
}

TEST_CASE("path_join: trailing slash on first", "[path]") {
    REQUIRE(path_join("foo/", "bar") == "foo/bar");
}

TEST_CASE("path_join: leading slash on second", "[path]") {
    REQUIRE(path_join("foo", "/bar") == "foo/bar");
}

TEST_CASE("path_join: both have slashes", "[path]") {
    REQUIRE(path_join("foo/", "/bar") == "foo/bar");
}

TEST_CASE("source_to_url: index.md → /", "[path]") {
    REQUIRE(source_to_url("src/index.md", "src") == "/");
}

TEST_CASE("source_to_url: about.md → /about/", "[path]") {
    REQUIRE(source_to_url("src/about.md", "src") == "/about/");
}

TEST_CASE("source_to_url: nested file", "[path]") {
    REQUIRE(source_to_url("src/posts/hello.md", "src") == "/posts/hello/");
}

TEST_CASE("source_to_url: nested index.md → /nested/", "[path]") {
    REQUIRE(source_to_url("src/blog/index.md", "src") == "/blog/");
}

TEST_CASE("url_to_output: root URL", "[path]") {
    REQUIRE(url_to_output("/", "output") == "output/index.html");
}

TEST_CASE("url_to_output: simple path", "[path]") {
    REQUIRE(url_to_output("/about/", "output") == "output/about/index.html");
}

TEST_CASE("url_to_output: nested path", "[path]") {
    REQUIRE(url_to_output("/posts/hello/", "output") == "output/posts/hello/index.html");
}

TEST_CASE("url_to_output: path without trailing slash", "[path]") {
    REQUIRE(url_to_output("/about", "output") == "output/about/index.html");
}

TEST_CASE("url_to_output: no leading slash", "[path]") {
    REQUIRE(url_to_output("about/", "output") == "output/about/index.html");
}

TEST_CASE("parent_dir: simple path", "[path]") {
    REQUIRE(parent_dir("foo/bar/baz.txt") == "foo/bar");
}

TEST_CASE("parent_dir: no slash", "[path]") {
    REQUIRE(parent_dir("file.txt") == ".");
}

TEST_CASE("replace_extension: has extension", "[path]") {
    REQUIRE(replace_extension("foo/bar.md", ".html") == "foo/bar.html");
}

TEST_CASE("replace_extension: no extension", "[path]") {
    REQUIRE(replace_extension("foo/bar", ".html") == "foo/bar.html");
}

TEST_CASE("replace_extension: dot in directory name", "[path]") {
    REQUIRE(replace_extension("foo.v2/bar.md", ".html") == "foo.v2/bar.html");
}
