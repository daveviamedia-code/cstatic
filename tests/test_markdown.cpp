#include <catch2/catch_test_macros.hpp>
#include "content/markdown.hpp"

using namespace cstatic;

TEST_CASE("Markdown: basic paragraphs", "[markdown]") {
    std::string md = "Hello world.\n\nSecond paragraph.";
    std::string html = render_markdown(md);
    REQUIRE(html.find("<p>Hello world.</p>") != std::string::npos);
    REQUIRE(html.find("<p>Second paragraph.</p>") != std::string::npos);
}

TEST_CASE("Markdown: headings", "[markdown]") {
    std::string md = "# Heading 1\n\n## Heading 2\n\n### Heading 3";
    std::string html = render_markdown(md);
    REQUIRE(html.find("<h1>Heading 1</h1>") != std::string::npos);
    REQUIRE(html.find("<h2>Heading 2</h2>") != std::string::npos);
    REQUIRE(html.find("<h3>Heading 3</h3>") != std::string::npos);
}

TEST_CASE("Markdown: links and emphasis", "[markdown]") {
    std::string md = "This is **bold** and *italic* and [a link](https://example.com).";
    std::string html = render_markdown(md);
    REQUIRE(html.find("<strong>bold</strong>") != std::string::npos);
    REQUIRE(html.find("<em>italic</em>") != std::string::npos);
    REQUIRE(html.find("<a href=\"https://example.com\">a link</a>") != std::string::npos);
}

TEST_CASE("Markdown: unordered list", "[markdown]") {
    std::string md = "- one\n- two\n- three";
    std::string html = render_markdown(md);
    REQUIRE(html.find("<ul>") != std::string::npos);
    REQUIRE(html.find("<li>one</li>") != std::string::npos);
    REQUIRE(html.find("<li>two</li>") != std::string::npos);
    REQUIRE(html.find("<li>three</li>") != std::string::npos);
    REQUIRE(html.find("</ul>") != std::string::npos);
}

TEST_CASE("Markdown: raw HTML passthrough", "[markdown]") {
    std::string md = "Before.\n\n<div class=\"custom\">Raw HTML</div>\n\nAfter.";
    std::string html = render_markdown(md);
    REQUIRE(html.find("<div class=\"custom\">Raw HTML</div>") != std::string::npos);
}

TEST_CASE("Markdown: code blocks", "[markdown]") {
    std::string md = "```\ncode here\n```";
    std::string html = render_markdown(md);
    REQUIRE(html.find("<code>") != std::string::npos);
    REQUIRE(html.find("code here") != std::string::npos);
}

TEST_CASE("Markdown: empty content", "[markdown]") {
    std::string html = render_markdown("");
    REQUIRE(html.empty());
}
