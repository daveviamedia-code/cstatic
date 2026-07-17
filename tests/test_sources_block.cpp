#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "config/config.hpp"
#include "content/sources_block.hpp"
#include "modules/seo_schema.hpp"

using cstatic::Config;
using cstatic::SourcesBlockProcessor;
using cstatic::modules::seo_schema::build_json_ld;

namespace {

static bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

} // namespace

TEST_CASE("Sources block processor", "[sources_block]") {
    SourcesBlockProcessor sbp;

    SECTION("No sources block leaves body unchanged") {
        std::string md = "# Hi\n\nJust text.";
        std::vector<nlohmann::json> schemas;
        nlohmann::json ctx;
        std::string out = sbp.process(md, schemas, ctx);
        REQUIRE(schemas.empty());
        REQUIRE(ctx.empty());
        REQUIRE(out == md);
    }

    SECTION("Empty block (open/close only) passes through with warning") {
        std::string md = "{% sources %}\n{% endsources %}";
        std::vector<nlohmann::json> schemas;
        nlohmann::json ctx;
        std::string out = sbp.process(md, schemas, ctx);
        REQUIRE(schemas.empty());
        REQUIRE_FALSE(contains(out, "{% sources"));
        REQUIRE_FALSE(contains(out, "{% endsources"));
    }

    SECTION("Block with only blank lines passes through") {
        std::string md = "{% sources %}\n\n   \n\n{% endschema %}";
        std::vector<nlohmann::json> schemas;
        nlohmann::json ctx;
        std::string out = sbp.process(md, schemas, ctx);
        REQUIRE(schemas.empty());
    }

    SECTION("Single markdown link") {
        std::string md =
            "{% sources %}\n"
            "- [Anthropic](https://www.anthropic.com)\n"
            "{% endsources %}";
        std::vector<nlohmann::json> schemas;
        nlohmann::json ctx;
        std::string out = sbp.process(md, schemas, ctx);

        REQUIRE(schemas.size() == 1);
        REQUIRE(schemas[0]["@type"] == "CreativeWork");
        REQUIRE(schemas[0]["citations"].is_array());
        REQUIRE(schemas[0]["citations"].size() == 1);
        REQUIRE(schemas[0]["citations"][0]["@type"] == "CreativeWork");
        REQUIRE(schemas[0]["citations"][0]["name"] == "Anthropic");
        REQUIRE(schemas[0]["citations"][0]["url"] == "https://www.anthropic.com");

        REQUIRE(contains(out, "<ol class=\"sources\">"));
        REQUIRE(contains(out, "<a href=\"https://www.anthropic.com\">Anthropic</a>"));
        REQUIRE(contains(out, "</ol>"));
        REQUIRE_FALSE(contains(out, "{% sources"));
    }

    SECTION("Bare URL autolinks and omits citation name") {
        std::string md =
            "{% sources %}\n"
            "https://example.com/report.pdf\n"
            "{% endsources %}";
        std::vector<nlohmann::json> schemas;
        nlohmann::json ctx;
        std::string out = sbp.process(md, schemas, ctx);

        REQUIRE(schemas.size() == 1);
        const auto& cite = schemas[0]["citations"][0];
        REQUIRE(cite["url"] == "https://example.com/report.pdf");
        REQUIRE_FALSE(cite.contains("name"));

        REQUIRE(contains(out, "<a href=\"https://example.com/report.pdf\">https://example.com/report.pdf</a>"));
    }

    SECTION("Markdown link with trailing annotation stays in HTML only") {
        std::string md =
            "{% sources %}\n"
            "- [Smith 2023](https://example.com/smith) — key finding\n"
            "{% endsources %}";
        std::vector<nlohmann::json> schemas;
        nlohmann::json ctx;
        std::string out = sbp.process(md, schemas, ctx);

        REQUIRE(schemas.size() == 1);
        REQUIRE(schemas[0]["citations"][0]["name"] == "Smith 2023");
        REQUIRE_FALSE(contains(schemas[0]["citations"][0]["name"].get<std::string>(), "key finding"));

        REQUIRE(contains(out, "— key finding"));
    }

    SECTION("Mixed list markers (-, *, +) and bare lines") {
        std::string md =
            "{% sources %}\n"
            "- [A](https://a.example.com)\n"
            "* [B](https://b.example.com)\n"
            "+ https://c.example.com\n"
            "{% endsources %}";
        std::vector<nlohmann::json> schemas;
        nlohmann::json ctx;
        std::string out = sbp.process(md, schemas, ctx);

        REQUIRE(schemas[0]["citations"].size() == 3);
        REQUIRE(schemas[0]["citations"][0]["name"] == "A");
        REQUIRE(schemas[0]["citations"][1]["name"] == "B");
        REQUIRE(schemas[0]["citations"][2]["url"] == "https://c.example.com");
    }

    SECTION("Multiple list items in one block preserve order") {
        std::string md =
            "{% sources %}\n"
            "- [First](https://first.example.com)\n"
            "- [Second](https://second.example.com)\n"
            "- [Third](https://third.example.com)\n"
            "{% endsources %}";
        std::vector<nlohmann::json> schemas;
        nlohmann::json ctx;
        sbp.process(md, schemas, ctx);

        REQUIRE(schemas[0]["citations"].size() == 3);
        REQUIRE(schemas[0]["citations"][0]["name"] == "First");
        REQUIRE(schemas[0]["citations"][1]["name"] == "Second");
        REQUIRE(schemas[0]["citations"][2]["name"] == "Third");
    }

    SECTION("Multiple sources blocks each emit their own schema; ctx concatenates") {
        std::string md =
            "{% sources %}\n"
            "- [One](https://one.example.com)\n"
            "{% endsources %}\n\n"
            "between\n\n"
            "{% sources %}\n"
            "- [Two](https://two.example.com)\n"
            "{% endsources %}";
        std::vector<nlohmann::json> schemas;
        nlohmann::json ctx;
        std::string out = sbp.process(md, schemas, ctx);

        REQUIRE(schemas.size() == 2);
        REQUIRE(schemas[0]["citations"][0]["name"] == "One");
        REQUIRE(schemas[1]["citations"][0]["name"] == "Two");
        REQUIRE(ctx.is_array());
        REQUIRE(ctx.size() == 2);
        REQUIRE(ctx[0]["text"] == "One");
        REQUIRE(ctx[1]["text"] == "Two");
        REQUIRE(contains(out, "between"));
    }

    SECTION("Unclosed block leaves opener verbatim") {
        std::string md = "Before {% sources %}\n- [X](https://x.example.com)\n";
        std::vector<nlohmann::json> schemas;
        nlohmann::json ctx;
        std::string out = sbp.process(md, schemas, ctx);

        REQUIRE(schemas.empty());
        REQUIRE(contains(out, "{% sources"));
        REQUIRE(contains(out, "Before"));
    }

    SECTION("Schema structure: CreativeWork with citations array") {
        std::string md =
            "{% sources %}\n"
            "- [NIST](https://www.nist.gov)\n"
            "{% endsources %}";
        std::vector<nlohmann::json> schemas;
        nlohmann::json ctx;
        sbp.process(md, schemas, ctx);

        REQUIRE(schemas.size() == 1);
        const auto& s = schemas[0];
        REQUIRE(s["@context"] == "https://schema.org");
        REQUIRE(s["@type"] == "CreativeWork");
        REQUIRE(s["citations"].is_array());
        REQUIRE(s["citations"][0]["@type"] == "CreativeWork");
        REQUIRE(s["citations"][0]["name"] == "NIST");
        REQUIRE(s["citations"][0]["url"] == "https://www.nist.gov");
    }

    SECTION("page.sources ctx shape is [{text, url, note}]") {
        std::string md =
            "{% sources %}\n"
            "- [Cited Paper](https://example.com/paper) — annotation\n"
            "{% endsources %}";
        std::vector<nlohmann::json> schemas;
        nlohmann::json ctx;
        sbp.process(md, schemas, ctx);

        REQUIRE(ctx.is_array());
        REQUIRE(ctx.size() == 1);
        REQUIRE(ctx[0]["text"] == "Cited Paper");
        REQUIRE(ctx[0]["url"] == "https://example.com/paper");
        REQUIRE(ctx[0]["note"] == "— annotation");
    }

    SECTION("End-to-end: schemas flow into build_json_ld via schema_extra") {
        std::string md =
            "{% sources %}\n"
            "- [Source](https://example.com/source)\n"
            "{% endsources %}";
        std::vector<nlohmann::json> schemas;
        nlohmann::json ctx;
        sbp.process(md, schemas, ctx);
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
        REQUIRE(contains(html, "\"@type\": \"CreativeWork\""));
        REQUIRE(contains(html, "\"citations\""));
        REQUIRE(contains(html, "https://example.com/source"));
    }
}
