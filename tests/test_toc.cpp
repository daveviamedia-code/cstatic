#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>

#include "content/toc.hpp"

using cstatic::build_toc;
using cstatic::render_toc_html;
using cstatic::replace_toc_markers;
using cstatic::to_json;

TEST_CASE("Auto Table of Contents", "[toc]") {

    SECTION("basic: two h2 headings -> flat tree with injected IDs") {
        std::string html =
            "<p>Intro.</p>"
            "<h2>First Section</h2>"
            "<p>Body one.</p>"
            "<h2>Second Section</h2>"
            "<p>Body two.</p>";

        auto toc = build_toc(html);
        REQUIRE(toc.size() == 2);
        REQUIRE(toc[0].id == "first-section");
        REQUIRE(toc[0].text == "First Section");
        REQUIRE(toc[0].level == 2);
        REQUIRE(toc[0].children.empty());
        REQUIRE(toc[1].id == "second-section");
        REQUIRE(toc[1].text == "Second Section");

        // IDs injected into the HTML.
        REQUIRE(html.find("<h2 id=\"first-section\">") != std::string::npos);
        REQUIRE(html.find("<h2 id=\"second-section\">") != std::string::npos);
    }

    SECTION("ID generation: slugifies heading text") {
        std::string html = "<h2>Hello, World!</h2>";
        auto toc = build_toc(html);
        REQUIRE(toc.size() == 1);
        REQUIRE(toc[0].id == "hello-world");
    }

    SECTION("duplicate headings get -1, -2 suffixes") {
        std::string html =
            "<h2>Intro</h2>"
            "<h2>Intro</h2>"
            "<h2>Intro</h2>";

        auto toc = build_toc(html);
        REQUIRE(toc.size() == 3);
        REQUIRE(toc[0].id == "intro");
        REQUIRE(toc[1].id == "intro-1");
        REQUIRE(toc[2].id == "intro-2");

        // Injected IDs match.
        REQUIRE(html.find("id=\"intro\"") != std::string::npos);
        REQUIRE(html.find("id=\"intro-1\"") != std::string::npos);
        REQUIRE(html.find("id=\"intro-2\"") != std::string::npos);
    }

    SECTION("collision suffixes match G8 passage index IDs") {
        // Same heading sequence that G8 would process — IDs must align.
        std::string html =
            "<h2>Conclusion</h2><p>a</p>"
            "<h2>Conclusion</h2><p>b</p>";

        auto toc = build_toc(html);
        REQUIRE(toc[0].id == "conclusion");
        REQUIRE(toc[1].id == "conclusion-1");
    }

    SECTION("nested tree: h2 with h3 children") {
        std::string html =
            "<h2>Top</h2>"
            "<h3>Sub A</h3>"
            "<h3>Sub B</h3>"
            "<h2>Top 2</h2>";

        auto toc = build_toc(html);
        REQUIRE(toc.size() == 2);
        REQUIRE(toc[0].text == "Top");
        REQUIRE(toc[0].children.size() == 2);
        REQUIRE(toc[0].children[0].text == "Sub A");
        REQUIRE(toc[0].children[0].level == 3);
        REQUIRE(toc[0].children[1].text == "Sub B");
        REQUIRE(toc[1].text == "Top 2");
        REQUIRE(toc[1].children.empty());
    }

    SECTION("skip levels: h2 then h4 nests correctly") {
        std::string html =
            "<h2>Parent</h2>"
            "<h4>Grandchild</h4>";

        auto toc = build_toc(html);
        REQUIRE(toc.size() == 1);
        REQUIRE(toc[0].text == "Parent");
        REQUIRE(toc[0].children.size() == 1);
        REQUIRE(toc[0].children[0].text == "Grandchild");
        REQUIRE(toc[0].children[0].level == 4);
    }

    SECTION("deep nesting: h2 > h3 > h4") {
        std::string html =
            "<h2>A</h2>"
            "<h3>B</h3>"
            "<h4>C</h4>";

        auto toc = build_toc(html);
        REQUIRE(toc.size() == 1);
        REQUIRE(toc[0].children.size() == 1);
        REQUIRE(toc[0].children[0].children.size() == 1);
        REQUIRE(toc[0].children[0].children[0].text == "C");
    }

    SECTION("existing id attributes are preserved") {
        std::string html = "<h2 id=\"my-custom-id\">Heading</h2>";

        auto toc = build_toc(html);
        REQUIRE(toc.size() == 1);
        REQUIRE(toc[0].id == "my-custom-id");
        // HTML unchanged — no double id injection.
        REQUIRE(html.find("id=\"my-custom-id\"") != std::string::npos);
        REQUIRE(html.find("id=\"heading\"") == std::string::npos);
    }

    SECTION("heading with class attr but no id gets id injected") {
        std::string html = "<h2 class=\"section\">Named</h2>";

        auto toc = build_toc(html);
        REQUIRE(toc[0].id == "named");
        REQUIRE(html.find("class=\"section\" id=\"named\"") != std::string::npos);
    }

    SECTION("h1 is skipped (page title, not TOC entry)") {
        std::string html =
            "<h1>Page Title</h1>"
            "<h2>Section</h2>";

        auto toc = build_toc(html);
        REQUIRE(toc.size() == 1);
        REQUIRE(toc[0].text == "Section");
    }

    SECTION("nested HTML inside heading is stripped for text") {
        std::string html = "<h2><a href=\"/x\">Linked Heading</a></h2>";

        auto toc = build_toc(html);
        REQUIRE(toc.size() == 1);
        REQUIRE(toc[0].text == "Linked Heading");
        REQUIRE(toc[0].id == "linked-heading");
    }

    SECTION("no headings -> empty tree") {
        std::string html = "<p>Just text.</p><ul><li>x</li></ul>";
        auto toc = build_toc(html);
        REQUIRE(toc.empty());
    }

    SECTION("empty html -> empty tree") {
        std::string html;
        auto toc = build_toc(html);
        REQUIRE(toc.empty());
    }

    SECTION("heading text entirely punctuation -> 'section' placeholder") {
        std::string html = "<h2>!!!</h2>";
        auto toc = build_toc(html);
        REQUIRE(toc.size() == 1);
        REQUIRE(toc[0].id == "section");
    }

    SECTION("levels h2 through h6 all captured") {
        std::string html =
            "<h2>L2</h2>"
            "<h3>L3</h3>"
            "<h4>L4</h4>"
            "<h5>L5</h5>"
            "<h6>L6</h6>";

        auto toc = build_toc(html);
        // All nested under L2.
        REQUIRE(toc.size() == 1);
        REQUIRE(toc[0].level == 2);
        REQUIRE(toc[0].children[0].level == 3);
        REQUIRE(toc[0].children[0].children[0].level == 4);
        REQUIRE(toc[0].children[0].children[0].children[0].level == 5);
        REQUIRE(toc[0].children[0].children[0].children[0].children[0].level == 6);
    }

    SECTION("case-insensitive heading tags match uppercase <H2>") {
        std::string html = "<H2>Title</H2>";
        auto toc = build_toc(html);
        REQUIRE(toc.size() == 1);
        REQUIRE(toc[0].text == "Title");
        // ID still injected even with uppercase tag.
        REQUIRE(html.find("id=\"title\"") != std::string::npos);
    }

    SECTION("multiple h2 headings with h3 between get correct nesting") {
        std::string html =
            "<h2>A</h2>"
            "<h3>A1</h3>"
            "<h2>B</h2>"
            "<h3>B1</h3>"
            "<h3>B2</h3>";

        auto toc = build_toc(html);
        REQUIRE(toc.size() == 2);
        REQUIRE(toc[0].children.size() == 1);
        REQUIRE(toc[0].children[0].text == "A1");
        REQUIRE(toc[1].children.size() == 2);
        REQUIRE(toc[1].children[0].text == "B1");
        REQUIRE(toc[1].children[1].text == "B2");
    }

    SECTION("to_json produces expected nested array shape") {
        std::string html =
            "<h2>Alpha</h2>"
            "<h3>Beta</h3>";

        nlohmann::json j = to_json(build_toc(html));
        REQUIRE(j.is_array());
        REQUIRE(j.size() == 1);
        REQUIRE(j[0]["id"]   == "alpha");
        REQUIRE(j[0]["text"] == "Alpha");
        REQUIRE(j[0]["level"] == 2);
        REQUIRE(j[0]["children"].is_array());
        REQUIRE(j[0]["children"].size() == 1);
        REQUIRE(j[0]["children"][0]["id"] == "beta");
        REQUIRE(j[0]["children"][0]["level"] == 3);
    }

    SECTION("render_toc_html produces nav with nested ul") {
        std::string html =
            "<h2>Overview</h2>"
            "<h3>Details</h3>";

        auto toc = build_toc(html);
        std::string rendered = render_toc_html(toc);
        REQUIRE(rendered.find("<nav class=\"toc\">") != std::string::npos);
        REQUIRE(rendered.find("<a href=\"#overview\">Overview</a>") != std::string::npos);
        REQUIRE(rendered.find("<a href=\"#details\">Details</a>") != std::string::npos);
        REQUIRE(rendered.find("toc-level-2") != std::string::npos);
        REQUIRE(rendered.find("toc-level-3") != std::string::npos);
    }

    SECTION("render_toc_html on empty tree returns empty string") {
        std::string rendered = render_toc_html({});
        REQUIRE(rendered.empty());
    }

    SECTION("replace_toc_markers: <!--toc--> replaced with nav HTML") {
        std::string html =
            "<p>Before.</p>"
            "<!--toc-->"
            "<h2>Section</h2>"
            "<p>After.</p>";

        auto toc = build_toc(html);
        REQUIRE(!toc.empty());
        replace_toc_markers(html, toc);
        REQUIRE(html.find("<!--toc-->") == std::string::npos);
        REQUIRE(html.find("<nav class=\"toc\">") != std::string::npos);
        REQUIRE(html.find("Before.") != std::string::npos);
        REQUIRE(html.find("After.") != std::string::npos);
    }

    SECTION("replace_toc_markers: <!-- toc --> with spaces also matched") {
        std::string html =
            "<p>Intro.</p>"
            "<!-- toc -->"
            "<h2>Section</h2>";

        auto toc = build_toc(html);
        REQUIRE(!toc.empty());
        replace_toc_markers(html, toc);
        REQUIRE(html.find("<!-- toc -->") == std::string::npos);
        REQUIRE(html.find("<nav class=\"toc\">") != std::string::npos);
    }

    SECTION("replace_toc_markers: no marker leaves HTML unchanged") {
        std::string html = "<p>No marker here.</p><h2>Section</h2>";
        std::string before = html;
        auto toc = build_toc(html);
        replace_toc_markers(html, toc);
        // Only difference should be injected id attrs, not nav.
        REQUIRE(html.find("<nav class=\"toc\">") == std::string::npos);
    }

    SECTION("replace_toc_markers: empty toc is a no-op") {
        std::string html = "<p>No headings.</p><!--toc-->";
        std::string before = html;
        replace_toc_markers(html, {});
        REQUIRE(html == before);
    }

    SECTION("ID injection does not corrupt surrounding content") {
        std::string html =
            "<p>Before heading.</p>"
            "<h2>Test</h2>"
            "<p>After heading.</p>";

        auto toc = build_toc(html);
        REQUIRE(html.find("<p>Before heading.</p>") != std::string::npos);
        REQUIRE(html.find("<p>After heading.</p>") != std::string::npos);
        REQUIRE(html.find("<h2 id=\"test\">Test</h2>") != std::string::npos);
    }

    SECTION("full pipeline: build_toc + replace_toc_markers in one pass") {
        std::string html =
            "<p>Intro.</p>"
            "<!--toc-->"
            "<h2>Getting Started</h2>"
            "<p>Content.</p>"
            "<h3>Installation</h3>"
            "<p>More content.</p>"
            "<h2>Advanced</h2>";

        auto toc = build_toc(html);
        REQUIRE(toc.size() == 2);
        REQUIRE(toc[0].children.size() == 1);
        REQUIRE(toc[0].children[0].text == "Installation");

        replace_toc_markers(html, toc);

        // Marker replaced.
        REQUIRE(html.find("<!--toc-->") == std::string::npos);
        // Nav HTML present.
        REQUIRE(html.find("<nav class=\"toc\">") != std::string::npos);
        // IDs injected on headings.
        REQUIRE(html.find("id=\"getting-started\"") != std::string::npos);
        REQUIRE(html.find("id=\"installation\"") != std::string::npos);
        REQUIRE(html.find("id=\"advanced\"") != std::string::npos);
        // Surrounding content intact.
        REQUIRE(html.find("Intro.") != std::string::npos);
        REQUIRE(html.find("Content.") != std::string::npos);
    }
}
