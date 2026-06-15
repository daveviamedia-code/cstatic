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

// --- GFM extensions (enabled by default via cmark-gfm) ---

TEST_CASE("Markdown: GFM table extension", "[markdown][gfm]") {
    std::string md =
        "| Name  | Age |\n"
        "|-------|-----|\n"
        "| Alice | 30  |\n"
        "| Bob   | 25  |\n";
    std::string html = render_markdown(md);
    REQUIRE(html.find("<table>") != std::string::npos);
    REQUIRE(html.find("<th>Name</th>") != std::string::npos);
    REQUIRE(html.find("<td>Alice</td>") != std::string::npos);
    REQUIRE(html.find("</table>") != std::string::npos);
}

TEST_CASE("Markdown: GFM task list extension", "[markdown][gfm]") {
    std::string md = "- [x] Done\n- [ ] Todo\n";
    std::string html = render_markdown(md);
    REQUIRE(html.find("type=\"checkbox\"") != std::string::npos);
    REQUIRE(html.find("checked") != std::string::npos);
}

TEST_CASE("Markdown: GFM strikethrough extension", "[markdown][gfm]") {
    std::string md = "This is ~~deleted~~ text.";
    std::string html = render_markdown(md);
    REQUIRE(html.find("<del>deleted</del>") != std::string::npos);
}

// --- Syntax highlighting ---

TEST_CASE("Highlight: JS keywords get spans", "[highlight]") {
    std::string html =
        "<pre><code class=\"language-js\">var x = 1;</code></pre>";
    std::string out = highlight_code_blocks(html, "github");
    REQUIRE(out.find("hl-keyword") != std::string::npos);
    REQUIRE(out.find("hl-number") != std::string::npos);
}

TEST_CASE("Highlight: strings are wrapped", "[highlight]") {
    std::string html =
        "<pre><code class=\"language-py\">print(\"hello\")</code></pre>";
    std::string out = highlight_code_blocks(html, "github");
    REQUIRE(out.find("hl-string") != std::string::npos);
    REQUIRE(out.find("hello") != std::string::npos);
}

TEST_CASE("Highlight: comments are wrapped", "[highlight]") {
    std::string html =
        "<pre><code class=\"language-cpp\">// a comment\nint x;</code></pre>";
    std::string out = highlight_code_blocks(html, "github");
    REQUIRE(out.find("hl-comment") != std::string::npos);
    REQUIRE(out.find("hl-keyword") != std::string::npos);
}

TEST_CASE("Highlight: unknown language left plain", "[highlight]") {
    std::string html =
        "<pre><code class=\"language-brainfuck\">+++[+++] </code></pre>";
    std::string out = highlight_code_blocks(html, "github");
    // No spans added for unknown language
    REQUIRE(out.find("hl-") == std::string::npos);
}

TEST_CASE("Highlight: no language hint stays plain", "[highlight]") {
    std::string html = "<pre><code>plain code</code></pre>";
    std::string out = highlight_code_blocks(html, "github");
    REQUIRE(out.find("hl-") == std::string::npos);
    REQUIRE(out.find("plain code") != std::string::npos);
}

TEST_CASE("Highlight: CSS has theme rules", "[highlight]") {
    std::string css = highlight_css("github");
    REQUIRE(css.find("hl-keyword") != std::string::npos);
    REQUIRE(css.find("hl-comment") != std::string::npos);

    std::string dark = highlight_css("github-dark");
    REQUIRE(dark.find("0d1117") != std::string::npos);
}

TEST_CASE("Highlight: render_markdown with options highlights code", "[markdown][highlight]") {
    MarkdownOptions opts;
    opts.highlight_enabled = true;
    opts.highlight_style = "github";
    std::string md = "```js\nvar x = 1;\n```\n";
    std::string html = render_markdown(md, opts);
    REQUIRE(html.find("hl-keyword") != std::string::npos);
    REQUIRE(html.find("hl-number") != std::string::npos);
}
