#include <catch2/catch_test_macros.hpp>
#include "content/frontmatter.hpp"

using namespace cstatic;

TEST_CASE("Frontmatter: with all fields", "[frontmatter]") {
    std::string content = "---\n"
                          "title: Hello World\n"
                          "layout: custom\n"
                          "permalink: /custom/url/\n"
                          "date: \"2025-01-15\"\n"
                          "tags:\n"
                          "  - one\n"
                          "  - two\n"
                          "draft: false\n"
                          "---\n"
                          "Body content here.";
    auto result = parse_frontmatter(content, "test.md");
    REQUIRE(result.frontmatter.title == "Hello World");
    REQUIRE(result.frontmatter.layout == "custom");
    REQUIRE(result.frontmatter.permalink == "/custom/url/");
    REQUIRE(result.frontmatter.date == "2025-01-15");
    REQUIRE(result.frontmatter.tags.size() == 2);
    REQUIRE(result.frontmatter.tags[0] == "one");
    REQUIRE(result.frontmatter.tags[1] == "two");
    REQUIRE(result.frontmatter.draft == false);
    REQUIRE(result.body == "Body content here.");
}

TEST_CASE("Frontmatter: no frontmatter", "[frontmatter]") {
    std::string content = "Just regular content\nwith no frontmatter.";
    auto result = parse_frontmatter(content, "test.md");
    REQUIRE(result.frontmatter.title.empty());
    REQUIRE(result.frontmatter.layout == "default");
    REQUIRE(result.body == content);
}

TEST_CASE("Frontmatter: empty content", "[frontmatter]") {
    std::string content = "";
    auto result = parse_frontmatter(content, "test.md");
    REQUIRE(result.body.empty());
}

TEST_CASE("Frontmatter: draft true", "[frontmatter]") {
    std::string content = "---\n"
                          "title: Draft\n"
                          "draft: true\n"
                          "---\n"
                          "Draft content.";
    auto result = parse_frontmatter(content, "test.md");
    REQUIRE(result.frontmatter.draft == true);
    REQUIRE(result.frontmatter.title == "Draft");
    REQUIRE(result.body == "Draft content.");
}

TEST_CASE("Frontmatter: custom fields", "[frontmatter]") {
    std::string content = "---\n"
                          "title: Custom\n"
                          "author: Jane\n"
                          "category: tech\n"
                          "---\n"
                          "Content.";
    auto result = parse_frontmatter(content, "test.md");
    REQUIRE(result.frontmatter.title == "Custom");
    REQUIRE(result.frontmatter.custom.count("author") == 1);
    REQUIRE(result.frontmatter.custom["author"] == "Jane");
    REQUIRE(result.frontmatter.custom.count("category") == 1);
    REQUIRE(result.frontmatter.custom["category"] == "tech");
}

TEST_CASE("Frontmatter: only title", "[frontmatter]") {
    std::string content = "---\n"
                          "title: Simple\n"
                          "---\n"
                          "Simple body.";
    auto result = parse_frontmatter(content, "test.md");
    REQUIRE(result.frontmatter.title == "Simple");
    REQUIRE(result.frontmatter.layout == "default");
    REQUIRE(result.frontmatter.tags.empty());
    REQUIRE(result.body == "Simple body.");
}

TEST_CASE("Frontmatter: malformed YAML in frontmatter", "[frontmatter]") {
    std::string content = "---\n"
                          "title: [bad yaml\n"
                          "---\n"
                          "Content.";
    REQUIRE_THROWS_AS(parse_frontmatter(content, "bad.md"), std::runtime_error);
}

TEST_CASE("Frontmatter: unclosed frontmatter", "[frontmatter]") {
    std::string content = "---\n"
                          "title: No Close\n"
                          "Content without closing delimiter.";
    auto result = parse_frontmatter(content, "test.md");
    // Should treat entire content as body
    REQUIRE(result.body == content);
}

TEST_CASE("Frontmatter: closing at EOF without trailing newline", "[frontmatter]") {
    std::string content = "---\n"
                          "title: EOF Test\n"
                          "---\n"
                          "Body";
    auto result = parse_frontmatter(content, "test.md");
    REQUIRE(result.frontmatter.title == "EOF Test");
    // Note: body is everything after the last ---\n
}
