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
using cstatic::process_standalone_faq;
using cstatic::merge_faq_into_schema_extra;
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

TEST_CASE("Standalone FAQ extraction", "[faq_extraction]") {
    SchemaBlockProcessor sbp;

    SECTION("process_standalone_faq renders details + collects ctx and questions") {
        std::string body =
            "This is the intro prose.\n\n"
            "More preamble.\n\n"
            "##? What is C-Static?\n"
            "A fast C++ static site generator.\n\n"
            "##? Is it free?\n"
            "Yes, MIT licensed.\n";
        auto res = process_standalone_faq(body);

        REQUIRE(res.found);
        // Prefix prose preserved.
        REQUIRE(contains(res.body, "This is the intro prose."));
        REQUIRE(contains(res.body, "More preamble."));
        // Markers gone from body.
        REQUIRE_FALSE(contains(res.body, "##?"));
        // Visible HTML.
        REQUIRE(contains(res.body, "<section class=\"faq\">"));
        REQUIRE(contains(res.body, "<summary>What is C-Static?</summary>"));
        REQUIRE(contains(res.body, "<summary>Is it free?</summary>"));
        REQUIRE(contains(res.body, "</details>"));
        REQUIRE(contains(res.body, "</section>"));
        // Context: 2 entries with question + answer fields.
        REQUIRE(res.faq_ctx.is_array());
        REQUIRE(res.faq_ctx.size() == 2);
        REQUIRE(res.faq_ctx[0]["question"] == "What is C-Static?");
        REQUIRE(contains(res.faq_ctx[0]["answer_text"].get<std::string>(), "fast"));
        REQUIRE(res.faq_ctx[1]["question"] == "Is it free?");
        // Questions: 2 Question schema objects.
        REQUIRE(res.questions.size() == 2);
        REQUIRE(res.questions[0]["@type"] == "Question");
        REQUIRE(res.questions[0]["acceptedAnswer"]["@type"] == "Answer");
    }

    SECTION("No ##? questions returns found=false and body unchanged") {
        std::string body = "# Title\n\nNo questions here.\n";
        auto res = process_standalone_faq(body);
        REQUIRE_FALSE(res.found);
        REQUIRE(res.body == body);
        REQUIRE(res.faq_ctx.is_array());
        REQUIRE(res.faq_ctx.empty());
        REQUIRE(res.questions.empty());
    }

    SECTION("merge_faq_into_schema_extra creates a new FAQPage") {
        std::vector<nlohmann::json> questions;
        nlohmann::json q1;
        q1["@type"] = "Question";
        q1["name"]  = "Q1";
        questions.push_back(q1);

        nlohmann::json schema_extra = nlohmann::json::array();
        merge_faq_into_schema_extra(schema_extra, questions);

        REQUIRE(schema_extra.is_array());
        REQUIRE(schema_extra.size() == 1);
        REQUIRE(schema_extra[0]["@type"] == "FAQPage");
        REQUIRE(schema_extra[0]["mainEntity"].is_array());
        REQUIRE(schema_extra[0]["mainEntity"].size() == 1);
        REQUIRE(schema_extra[0]["mainEntity"][0]["name"] == "Q1");
    }

    SECTION("merge_faq_into_schema_extra appends to existing FAQPage mainEntity") {
        nlohmann::json schema_extra = nlohmann::json::array();
        nlohmann::json existing;
        existing["@context"] = "https://schema.org";
        existing["@type"]    = "FAQPage";
        existing["mainEntity"] = nlohmann::json::array();
        nlohmann::json q0;
        q0["@type"] = "Question";
        q0["name"]  = "Q0";
        existing["mainEntity"].push_back(q0);
        schema_extra.push_back(existing);

        std::vector<nlohmann::json> questions;
        nlohmann::json q1;
        q1["@type"] = "Question";
        q1["name"]  = "Q1";
        questions.push_back(q1);

        merge_faq_into_schema_extra(schema_extra, questions);

        REQUIRE(schema_extra.size() == 1);
        REQUIRE(schema_extra[0]["@type"] == "FAQPage");
        REQUIRE(schema_extra[0]["mainEntity"].size() == 2);
        REQUIRE(schema_extra[0]["mainEntity"][0]["name"] == "Q0");
        REQUIRE(schema_extra[0]["mainEntity"][1]["name"] == "Q1");
    }

    SECTION("End-to-end: G4 FAQPage block + trailing standalone ##? merge") {
        std::string md =
            "{% schema \"FAQPage\" %}\n"
            "##? Wrapped question\n"
            "Wrapped answer.\n"
            "{% endschema %}\n\n"
            "##? Standalone question\n"
            "Standalone answer.\n";
        std::vector<nlohmann::json> schemas;
        std::string transformed = sbp.process(md, schemas);
        REQUIRE(schemas.size() == 1);
        REQUIRE(schemas[0]["@type"] == "FAQPage");
        REQUIRE(schemas[0]["mainEntity"].size() == 1);

        auto sfaq = process_standalone_faq(transformed);
        REQUIRE(sfaq.found);
        REQUIRE(sfaq.questions.size() == 1);
        REQUIRE(sfaq.questions[0]["name"] == "Standalone question");

        nlohmann::json schema_extra = schemas;
        merge_faq_into_schema_extra(schema_extra, sfaq.questions);
        REQUIRE(schema_extra.size() == 1);
        REQUIRE(schema_extra[0]["@type"] == "FAQPage");
        REQUIRE(schema_extra[0]["mainEntity"].size() == 2);
        REQUIRE(schema_extra[0]["mainEntity"][0]["name"] == "Wrapped question");
        REQUIRE(schema_extra[0]["mainEntity"][1]["name"] == "Standalone question");
    }

    SECTION("Standalone FAQ flows into build_json_ld via merged schema_extra") {
        std::string body =
            "Intro.\n\n"
            "##? What is this?\n"
            "A FAQ entry.\n";
        auto sfaq = process_standalone_faq(body);
        REQUIRE(sfaq.found);

        nlohmann::json schema_extra = nlohmann::json::array();
        merge_faq_into_schema_extra(schema_extra, sfaq.questions);

        Config cfg;
        cfg.site_title = "S";
        cfg.site_base_url = "https://example.com";
        cfg.json_ld_enabled = true;

        nlohmann::json page;
        page["title"] = "P";
        page["url"] = "/p/";
        page["schema_extra"] = schema_extra;

        std::string html = build_json_ld(cfg, page, nlohmann::json::array());
        REQUIRE(contains(html, "\"@type\": \"FAQPage\""));
        REQUIRE(contains(html, "\"@type\": \"Question\""));
        REQUIRE(contains(html, "What is this?"));
    }
}
