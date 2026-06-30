#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "config/config.hpp"
#include "content/schema_blocks.hpp"
#include "modules/seo_schema.hpp"

using cstatic::Config;
using cstatic::SchemaBlockProcessor;
using cstatic::extract_faq_pairs;
using cstatic::modules::seo_schema::build_json_ld;

namespace {

static bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

} // namespace

TEST_CASE("Schema block processor", "[schema_blocks]") {
    SchemaBlockProcessor sbp;

    SECTION("No schema blocks leaves body unchanged") {
        std::string md = "# Hi\n\nJust text.";
        std::vector<nlohmann::json> schemas;
        std::string out = sbp.process(md, schemas);
        REQUIRE(schemas.empty());
        REQUIRE(out == md);
    }

    SECTION("FAQPage extracts questions and answers") {
        std::string md =
            "Intro.\n\n"
            "{% schema \"FAQPage\" %}\n"
            "##? What is C-Static?\n"
            "A fast C++ static site generator.\n\n"
            "##? Is it free?\n"
            "Yes, MIT licensed.\n"
            "{% endschema %}\n\n"
            "Outro.\n";
        std::vector<nlohmann::json> schemas;
        std::string out = sbp.process(md, schemas);

        REQUIRE(schemas.size() == 1);
        const auto& s = schemas[0];
        REQUIRE(s["@type"] == "FAQPage");
        REQUIRE(s["mainEntity"].is_array());
        REQUIRE(s["mainEntity"].size() == 2);
        REQUIRE(s["mainEntity"][0]["@type"] == "Question");
        REQUIRE(s["mainEntity"][0]["name"] == "What is C-Static?");
        REQUIRE(s["mainEntity"][0]["acceptedAnswer"]["@type"] == "Answer");
        REQUIRE(contains(s["mainEntity"][0]["acceptedAnswer"]["text"].get<std::string>(),
                         "fast"));
        REQUIRE(s["mainEntity"][1]["name"] == "Is it free?");

        // Visible HTML.
        REQUIRE(contains(out, "<section class=\"faq\">"));
        REQUIRE(contains(out, "<summary>What is C-Static?</summary>"));
        REQUIRE(contains(out, "</details>"));
        REQUIRE(contains(out, "</section>"));

        // Surrounding content preserved, tags consumed.
        REQUIRE(contains(out, "Intro."));
        REQUIRE(contains(out, "Outro."));
        REQUIRE_FALSE(contains(out, "{% schema"));
        REQUIRE_FALSE(contains(out, "{% endschema"));
        REQUIRE_FALSE(contains(out, "##?"));
    }

    SECTION("FAQPage block with no questions passes through") {
        std::string md = "{% schema \"FAQPage\" %}\nNo questions here.\n{% endschema %}";
        std::vector<nlohmann::json> schemas;
        std::string out = sbp.process(md, schemas);
        REQUIRE(schemas.empty());
        REQUIRE(contains(out, "No questions here."));
        REQUIRE_FALSE(contains(out, "{% schema"));
    }

    SECTION("HowTo extracts steps") {
        std::string md =
            "{% schema \"HowTo\" %}\n"
            "##! Install it\n"
            "Run `make`.\n\n"
            "##! Run it\n"
            "Type `./cstatic`.\n"
            "{% endschema %}";
        std::vector<nlohmann::json> schemas;
        std::string out = sbp.process(md, schemas);

        REQUIRE(schemas.size() == 1);
        const auto& s = schemas[0];
        REQUIRE(s["@type"] == "HowTo");
        REQUIRE(s["step"].is_array());
        REQUIRE(s["step"].size() == 2);
        REQUIRE(s["step"][0]["@type"] == "HowToStep");
        REQUIRE(s["step"][0]["name"] == "Install it");
        REQUIRE(contains(s["step"][0]["text"].get<std::string>(), "make"));

        REQUIRE(contains(out, "<ol class=\"howto\">"));
        REQUIRE(contains(out, "<h3>Install it</h3>"));
        REQUIRE(contains(out, "</ol>"));
        REQUIRE_FALSE(contains(out, "##!"));
    }

    SECTION("Review with attributes") {
        std::string md =
            "{% schema \"Review\" item=\"Widget\" rating=\"5\" %}\n"
            "This widget is great.\n"
            "{% endschema %}";
        std::vector<nlohmann::json> schemas;
        std::string out = sbp.process(md, schemas);

        REQUIRE(schemas.size() == 1);
        const auto& s = schemas[0];
        REQUIRE(s["@type"] == "Review");
        REQUIRE(s["itemReviewed"]["name"] == "Widget");
        REQUIRE(s["reviewRating"]["ratingValue"] == "5");
        REQUIRE(contains(s["reviewBody"].get<std::string>(), "great"));

        REQUIRE(contains(out, "<div class=\"review\" data-rating=\"5\">"));
        REQUIRE(contains(out, "</div>"));
    }

    SECTION("Review without rating omits data attribute") {
        std::string md =
            "{% schema \"Review\" item=\"Thing\" %}\nOk.\n{% endschema %}";
        std::vector<nlohmann::json> schemas;
        std::string out = sbp.process(md, schemas);
        REQUIRE(schemas.size() == 1);
        REQUIRE(schemas[0]["itemReviewed"]["name"] == "Thing");
        REQUIRE_FALSE(schemas[0].contains("reviewRating"));
        REQUIRE(contains(out, "<div class=\"review\">"));
        REQUIRE_FALSE(contains(out, "data-rating"));
    }

    SECTION("Unknown type passes through with no schema") {
        std::string md = "{% schema \"Mystery\" %}\nHello world.\n{% endschema %}";
        std::vector<nlohmann::json> schemas;
        std::string out = sbp.process(md, schemas);
        REQUIRE(schemas.empty());
        REQUIRE(contains(out, "Hello world."));
        REQUIRE_FALSE(contains(out, "{% schema"));
    }

    SECTION("Multiple schema blocks are all collected") {
        std::string md =
            "{% schema \"FAQPage\" %}\n##? Q1\nA1\n{% endschema %}\n\n"
            "between\n\n"
            "{% schema \"Review\" item=\"X\" rating=\"4\" %}\nBody.\n{% endschema %}";
        std::vector<nlohmann::json> schemas;
        std::string out = sbp.process(md, schemas);
        REQUIRE(schemas.size() == 2);
        REQUIRE(schemas[0]["@type"] == "FAQPage");
        REQUIRE(schemas[1]["@type"] == "Review");
        REQUIRE(contains(out, "between"));
    }

    SECTION("Unclosed schema block leaves opener verbatim") {
        std::string md = "Before {% schema \"FAQPage\" %}\n##? Q\nA\n";
        std::vector<nlohmann::json> schemas;
        std::string out = sbp.process(md, schemas);
        REQUIRE(schemas.empty());
        REQUIRE(contains(out, "{% schema"));
        REQUIRE(contains(out, "Before"));
    }

    SECTION("extract_faq_pairs helper") {
        std::string t =
            "##? Q1\nA1 line 1\nA1 line 2\n\n##? Q2\nA2\n";
        auto pairs = extract_faq_pairs(t);
        REQUIRE(pairs.size() == 2);
        REQUIRE(pairs[0].question == "Q1");
        REQUIRE(contains(pairs[0].answer_md, "A1 line 1"));
        REQUIRE(pairs[1].question == "Q2");
        REQUIRE(contains(pairs[1].answer_md, "A2"));
    }

    SECTION("Extracted schemas flow into build_json_ld via schema_extra") {
        std::string md = "{% schema \"FAQPage\" %}\n##? Q\nA\n{% endschema %}";
        std::vector<nlohmann::json> schemas;
        sbp.process(md, schemas);
        REQUIRE(schemas.size() == 1);

        Config cfg;
        cfg.site_title = "S";
        cfg.site_base_url = "https://example.com";
        cfg.json_ld_enabled = true;

        nlohmann::json page;
        page["title"] = "P";
        page["url"] = "/p/";
        page["schema_extra"] = schemas;

        std::string html = build_json_ld(cfg, page, nlohmann::json::array());
        REQUIRE(contains(html, "\"@type\": \"FAQPage\""));
        REQUIRE(contains(html, "\"@type\": \"Question\""));
    }
}
