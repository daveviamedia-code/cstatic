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
    REQUIRE(result.frontmatter.custom.contains("author"));
    REQUIRE(result.frontmatter.custom["author"] == "Jane");
    REQUIRE(result.frontmatter.custom.contains("category"));
    REQUIRE(result.frontmatter.custom["category"] == "tech");
}

TEST_CASE("Frontmatter: array custom field", "[frontmatter]") {
    std::string content = "---\n"
                          "title: Array Test\n"
                          "authors:\n"
                          "  - Alice\n"
                          "  - Bob\n"
                          "---\n"
                          "Content.";
    auto result = parse_frontmatter(content, "test.md");
    REQUIRE(result.frontmatter.custom.contains("authors"));
    REQUIRE(result.frontmatter.custom["authors"].is_array());
    REQUIRE(result.frontmatter.custom["authors"].size() == 2);
    REQUIRE(result.frontmatter.custom["authors"][0] == "Alice");
    REQUIRE(result.frontmatter.custom["authors"][1] == "Bob");
}

TEST_CASE("Frontmatter: nested map custom field", "[frontmatter]") {
    std::string content = "---\n"
                          "title: Map Test\n"
                          "meta:\n"
                          "  key1: val1\n"
                          "  key2: val2\n"
                          "---\n"
                          "Content.";
    auto result = parse_frontmatter(content, "test.md");
    REQUIRE(result.frontmatter.custom.contains("meta"));
    REQUIRE(result.frontmatter.custom["meta"].is_object());
    REQUIRE(result.frontmatter.custom["meta"]["key1"] == "val1");
    REQUIRE(result.frontmatter.custom["meta"]["key2"] == "val2");
}

TEST_CASE("Frontmatter: type preservation in custom fields", "[frontmatter]") {
    std::string content = "---\n"
                          "title: Types\n"
                          "count: 42\n"
                          "active: true\n"
                          "score: 3.14\n"
                          "---\n"
                          "Content.";
    auto result = parse_frontmatter(content, "test.md");
    REQUIRE(result.frontmatter.custom["count"].is_number_integer());
    REQUIRE(result.frontmatter.custom["count"].get<int>() == 42);
    REQUIRE(result.frontmatter.custom["active"].is_boolean());
    REQUIRE(result.frontmatter.custom["active"].get<bool>() == true);
    REQUIRE(result.frontmatter.custom["score"].is_number_float());
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

TEST_CASE("Frontmatter: aliases array", "[frontmatter]") {
    std::string content = "---\n"
                          "title: Aliased\n"
                          "aliases: [/old-url/, /another/old/path/]\n"
                          "---\n"
                          "Content.";
    auto result = parse_frontmatter(content, "test.md");
    REQUIRE(result.frontmatter.aliases.size() == 2);
    REQUIRE(result.frontmatter.aliases[0] == "/old-url/");
    REQUIRE(result.frontmatter.aliases[1] == "/another/old/path/");
    REQUIRE_FALSE(result.frontmatter.custom.contains("aliases"));
}

TEST_CASE("Frontmatter: SEO and sitemap fields", "[frontmatter]") {
    std::string content = "---\n"
                          "title: SEO Post\n"
                          "description: A great description\n"
                          "image: /images/hero.png\n"
                          "canonical: https://example.com/custom-canonical\n"
                          "sitemap_changefreq: monthly\n"
                          "sitemap_priority: 0.8\n"
                          "---\n"
                          "Content.";
    auto result = parse_frontmatter(content, "test.md");
    REQUIRE(result.frontmatter.description == "A great description");
    REQUIRE(result.frontmatter.image == "/images/hero.png");
    REQUIRE(result.frontmatter.canonical == "https://example.com/custom-canonical");
    REQUIRE(result.frontmatter.sitemap_changefreq == "monthly");
    REQUIRE(result.frontmatter.sitemap_priority == "0.8");
    REQUIRE_FALSE(result.frontmatter.custom.contains("description"));
    REQUIRE_FALSE(result.frontmatter.custom.contains("image"));
    REQUIRE_FALSE(result.frontmatter.custom.contains("canonical"));
    REQUIRE_FALSE(result.frontmatter.custom.contains("sitemap_changefreq"));
    REQUIRE_FALSE(result.frontmatter.custom.contains("sitemap_priority"));
}

TEST_CASE("Frontmatter: numeric sitemap_priority", "[frontmatter]") {
    std::string content = "---\n"
                          "title: Numeric Priority\n"
                          "sitemap_priority: 0.8\n"
                          "---\n"
                          "Content.";
    auto result = parse_frontmatter(content, "test.md");
    REQUIRE(result.frontmatter.sitemap_priority == "0.8");
}
