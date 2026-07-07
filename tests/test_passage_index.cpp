#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>

#include "content/passage_index.hpp"

using cstatic::extract_passages;
using cstatic::to_json;

TEST_CASE("Passage index extraction", "[passage_index]") {

    SECTION("basic extraction: two headings -> two ordered passages") {
        std::string html =
            "<p>Intro paragraph.</p>"
            "<h2>First Section</h2>"
            "<p>Body of first section.</p>"
            "<h2>Second Section</h2>"
            "<p>Body of second section.</p>";

        auto passages = extract_passages(html);
        REQUIRE(passages.size() == 2);
        REQUIRE(passages[0].heading == "First Section");
        REQUIRE(passages[0].level == 2);
        REQUIRE(passages[0].text == "Body of first section.");
        REQUIRE(passages[1].heading == "Second Section");
        REQUIRE(passages[1].text == "Body of second section.");
    }

    SECTION("h1 is skipped (page title, not a passage)") {
        std::string html =
            "<h1>Page Title</h1>"
            "<p>Lead.</p>"
            "<h2>Section</h2>"
            "<p>Body.</p>";

        auto passages = extract_passages(html);
        REQUIRE(passages.size() == 1);
        REQUIRE(passages[0].heading == "Section");
        // Body of the h2 — must not include the h1's lead paragraph.
        REQUIRE(passages[0].text == "Body.");
    }

    SECTION("levels 2, 3, 4 captured with their level") {
        std::string html =
            "<h2>L2</h2><p>a</p>"
            "<h3>L3</h3><p>b</p>"
            "<h4>L4</h4><p>c</p>";

        auto passages = extract_passages(html);
        REQUIRE(passages.size() == 3);
        REQUIRE(passages[0].level == 2);
        REQUIRE(passages[1].level == 3);
        REQUIRE(passages[2].level == 4);
    }

    SECTION("heading text is slugified into id") {
        std::string html = "<h2>Hello, World!</h2><p>Body.</p>";

        auto passages = extract_passages(html);
        REQUIRE(passages.size() == 1);
        REQUIRE(passages[0].id == "hello-world");
    }

    SECTION("duplicate headings get -1, -2 suffixes") {
        std::string html =
            "<h2>Conclusion</h2><p>First ending.</p>"
            "<h2>Conclusion</h2><p>Second ending.</p>"
            "<h2>Conclusion</h2><p>Third ending.</p>";

        auto passages = extract_passages(html);
        REQUIRE(passages.size() == 3);
        REQUIRE(passages[0].id == "conclusion");
        REQUIRE(passages[1].id == "conclusion-1");
        REQUIRE(passages[2].id == "conclusion-2");
    }

    SECTION("text truncated at 500 chars with ellipsis") {
        // Build a body well over 500 chars.
        std::string body(600, 'x');
        std::string html = "<h2>Big</h2><p>" + body + "</p>";

        auto passages = extract_passages(html);
        REQUIRE(passages.size() == 1);
        REQUIRE(passages[0].text.size() <= 503);  // substr(<=500) + "..."
        REQUIRE(passages[0].text.size() >= 3);
        // Truncation marker is present.
        REQUIRE(passages[0].text.substr(passages[0].text.size() - 3) == "...");
    }

    SECTION("heading immediately followed by next heading -> empty text") {
        std::string html =
            "<h2>One</h2>"
            "<h2>Two</h2>"
            "<p>Body of two.</p>";

        auto passages = extract_passages(html);
        REQUIRE(passages.size() == 2);
        REQUIRE(passages[0].heading == "One");
        REQUIRE(passages[0].text.empty());
        REQUIRE(passages[1].heading == "Two");
        REQUIRE(passages[1].text == "Body of two.");
    }

    SECTION("no headings -> empty vector") {
        std::string html = "<p>Just a paragraph.</p><ul><li>x</li></ul>";
        auto passages = extract_passages(html);
        REQUIRE(passages.empty());
    }

    SECTION("empty html -> empty vector") {
        auto passages = extract_passages("");
        REQUIRE(passages.empty());
    }

    SECTION("nested HTML inside heading is stripped to text") {
        std::string html = "<h2><a href=\"/x\">Linked Heading</a></h2><p>Body.</p>";

        auto passages = extract_passages(html);
        REQUIRE(passages.size() == 1);
        REQUIRE(passages[0].heading == "Linked Heading");
        REQUIRE(passages[0].id == "linked-heading");
    }

    SECTION("to_json produces expected array shape") {
        std::string html =
            "<h2>Alpha</h2><p>Alpha body.</p>"
            "<h3>Beta</h3><p>Beta body.</p>";

        nlohmann::json j = to_json(extract_passages(html));
        REQUIRE(j.is_array());
        REQUIRE(j.size() == 2);
        REQUIRE(j[0]["id"]      == "alpha");
        REQUIRE(j[0]["heading"] == "Alpha");
        REQUIRE(j[0]["text"]    == "Alpha body.");
        REQUIRE(j[0]["level"]   == 2);
        REQUIRE(j[1]["id"]      == "beta");
        REQUIRE(j[1]["level"]   == 3);
    }

    SECTION("case-insensitive heading tags match uppercase <H2>") {
        std::string html = "<H2>Title</H2><p>Body.</p>";
        auto passages = extract_passages(html);
        REQUIRE(passages.size() == 1);
        REQUIRE(passages[0].heading == "Title");
    }

    SECTION("heading with attributes (class, id) is parsed") {
        std::string html = "<h2 class=\"section\" id=\"custom\">Named</h2><p>Body.</p>";
        auto passages = extract_passages(html);
        REQUIRE(passages.size() == 1);
        REQUIRE(passages[0].heading == "Named");
        // slug comes from heading text, not the id attr (G11 will align these later).
        REQUIRE(passages[0].id == "named");
    }
}
