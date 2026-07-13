#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "config/config.hpp"
#include "modules/seo_schema.hpp"

using cstatic::Config;
using cstatic::modules::seo_schema::build_citation_tags;
using cstatic::modules::seo_schema::build_json_ld;
using cstatic::modules::seo_schema::build_org_context;
using cstatic::modules::seo_schema::build_organization_script;
using cstatic::modules::seo_schema::build_website_script;
using cstatic::modules::seo_schema::validate;
using cstatic::modules::seo_schema::validate_organization;

namespace {

static bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

static Config base_config() {
    Config cfg;
    cfg.site_title = "My Site";
    cfg.site_base_url = "https://example.com";
    cfg.json_ld_enabled = true;
    return cfg;
}

static nlohmann::json make_page(const std::string& title, const std::string& url,
                                const std::string& date) {
    nlohmann::json p;
    p["title"] = title;
    p["url"] = url;
    p["date"] = date;
    return p;
}

// Return the JSON content of the Nth (0-indexed) JSON-LD <script> block.
// Null on out-of-range or parse failure.
static nlohmann::json extract_script(const std::string& html, size_t index) {
    const std::string open = "<script type=\"application/ld+json\">\n";
    const std::string close = "\n</script>\n";
    size_t pos = 0;
    for (size_t i = 0; i <= index; ++i) {
        size_t start = html.find(open, pos);
        if (start == std::string::npos) return nullptr;
        size_t json_start = start + open.size();
        size_t end = html.find(close, json_start);
        if (end == std::string::npos) return nullptr;
        if (i == index) {
            return nlohmann::json::parse(html.substr(json_start, end - json_start));
        }
        pos = end + close.size();
    }
    return nullptr;
}

static int count_scripts(const std::string& html) {
    const std::string open = "<script type=\"application/ld+json\">\n";
    int n = 0;
    size_t pos = 0;
    while ((pos = html.find(open, pos)) != std::string::npos) {
        ++n;
        pos += open.size();
    }
    return n;
}

} // anonymous namespace

TEST_CASE("seo_schema: disabled returns empty", "[seo_schema]") {
    Config cfg = base_config();
    cfg.json_ld_enabled = false;
    auto page = make_page("P", "/p/", "2025-01-01");
    REQUIRE(build_json_ld(cfg, page, nlohmann::json::array()).empty());
}

TEST_CASE("seo_schema: WebSite schema always emitted with site title", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("Hello", "/hello/", "2025-01-01");
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json ws = extract_script(out, 0);
    REQUIRE(ws["@type"] == "WebSite");
    REQUIRE(ws["name"] == "My Site");
    REQUIRE(ws["url"] == "https://example.com");
    // Standalone helper agrees.
    REQUIRE(contains(build_website_script(cfg), "\"WebSite\""));
}

TEST_CASE("seo_schema: Organization toggles on org_name", "[seo_schema]") {
    // Absent when org_name empty — script 1 is the page schema (WebPage).
    {
        Config cfg = base_config();
        auto page = make_page("P", "/p/", "2025-01-01");
        std::string out = build_json_ld(cfg, page, nlohmann::json::array());
        nlohmann::json s1 = extract_script(out, 1);
        REQUIRE(s1["@type"] == "WebPage");
        REQUIRE(build_organization_script(cfg).empty());
    }
    // Present with every field populated.
    {
        Config cfg = base_config();
        cfg.org_name = "Acme Inc";
        cfg.org_legal_name = "Acme Incorporated";
        cfg.org_logo = "/logo.png";
        cfg.org_founding_date = "2001-02-03";
        cfg.org_founders = {"Alice", "Bob"};
        cfg.org_same_as = {"https://twitter.com/acme"};
        cfg.org_url = "https://acme.example.com";
        auto page = make_page("P", "/p/", "2025-01-01");
        std::string out = build_json_ld(cfg, page, nlohmann::json::array());
        nlohmann::json org = extract_script(out, 1);  // after WebSite
        REQUIRE(org["@type"] == "Organization");
        REQUIRE(org["name"] == "Acme Inc");
        REQUIRE(org["legalName"] == "Acme Incorporated");
        REQUIRE(org["logo"] == "https://example.com/logo.png");
        REQUIRE(org["foundingDate"] == "2001-02-03");
        REQUIRE(org["url"] == "https://acme.example.com");
        REQUIRE(org["founder"].size() == 2);
        REQUIRE(org["founder"][0]["@type"] == "Person");
        REQUIRE(org["founder"][0]["name"] == "Alice");
        REQUIRE(org["sameAs"][0] == "https://twitter.com/acme");
    }
}

TEST_CASE("seo_schema: WebSite SearchAction when template set", "[seo_schema]") {
    Config cfg = base_config();
    cfg.website_search_url_template = "/search?q={search_term_string}";
    auto page = make_page("P", "/p/", "2025-01-01");
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json ws = extract_script(out, 0);
    REQUIRE(ws["potentialAction"]["@type"] == "SearchAction");
    REQUIRE(ws["potentialAction"]["target"] ==
            "https://example.com/search?q={search_term_string}");
    REQUIRE(ws["potentialAction"]["query-input"] ==
            "required name=search_term_string");
}

TEST_CASE("seo_schema: default WebPage for root-level page", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("Home", "/", "2025-01-01");
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json p = extract_script(out, 1);
    REQUIRE(p["@type"] == "WebPage");
    REQUIRE(p["name"] == "Home");
    REQUIRE(p["url"] == "https://example.com/");
    // Root page gets no breadcrumb.
    REQUIRE(count_scripts(out) == 2);
}

TEST_CASE("seo_schema: BlogPosting auto-default for /posts/ URL", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("Post", "/posts/hello/", "2025-01-01");
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json p = extract_script(out, 1);
    REQUIRE(p["@type"] == "BlogPosting");
}

TEST_CASE("seo_schema: explicit type overrides URL heuristic", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("P", "/posts/hello/", "2025-01-01");
    page["type"] = "Article";
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json p = extract_script(out, 1);
    REQUIRE(p["@type"] == "Article");
}

TEST_CASE("seo_schema: explicit schema.@type wins over page.type", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("P", "/posts/hello/", "2025-01-01");
    page["type"] = "Article";
    page["schema"] = nlohmann::json{{"@type", "Product"}};
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json p = extract_script(out, 1);
    REQUIRE(p["@type"] == "Product");
}

TEST_CASE("seo_schema: BlogPosting maps headline/author/publisher/keywords",
          "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("Hello World", "/posts/hello/", "2025-01-02");
    page["author"] = "Jane Doe";
    page["tags"] = {"foo", "bar"};
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json p = extract_script(out, 1);
    REQUIRE(p["headline"] == "Hello World");
    REQUIRE(p["datePublished"] == "2025-01-02");
    REQUIRE(p["dateModified"] == "2025-01-02");
    REQUIRE(p["author"]["@type"] == "Person");
    REQUIRE(p["author"]["name"] == "Jane Doe");
    REQUIRE(p["publisher"]["@type"] == "Organization");
    REQUIRE(p["publisher"]["name"] == "My Site");  // falls back to site_title
    REQUIRE(p["keywords"] == "foo, bar");          // comma-joined tags
    REQUIRE(p["mainEntityOfPage"]["@id"] ==
            "https://example.com/posts/hello/");
}

TEST_CASE("seo_schema: publisher uses Organization when org_name set",
          "[seo_schema]") {
    Config cfg = base_config();
    cfg.org_name = "Acme";
    cfg.org_logo = "/logo.png";
    auto page = make_page("Hello", "/posts/hello/", "2025-01-02");
    page["author"] = "Jane";
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    // Script 0 = WebSite, 1 = Organization, 2 = BlogPosting.
    nlohmann::json p = extract_script(out, 2);
    REQUIRE(p["publisher"]["name"] == "Acme");
    REQUIRE(p["publisher"]["logo"] == "https://example.com/logo.png");
}

TEST_CASE("seo_schema: schema override deep-merges", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("Hello", "/posts/hello/", "2025-01-02");
    page["schema"] = nlohmann::json{{"description", "Custom description"}};
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json p = extract_script(out, 1);
    REQUIRE(p["@type"] == "BlogPosting");
    REQUIRE(p["headline"] == "Hello");               // auto field preserved
    REQUIRE(p["description"] == "Custom description");  // override wins
}

TEST_CASE("seo_schema: schema_extra emits verbatim blocks", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("P", "/", "2025-01-01");  // root → no breadcrumb
    page["schema_extra"] = nlohmann::json::array({
        nlohmann::json{{"@type", "FAQPage"}, {"name", "Q1"}},
        nlohmann::json{{"@type", "Event"}, {"name", "E1"}},
    });
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    // WebSite + page schema + 2 extras = 4
    REQUIRE(count_scripts(out) == 4);
    nlohmann::json e0 = extract_script(out, 2);
    nlohmann::json e1 = extract_script(out, 3);
    REQUIRE(e0["@type"] == "FAQPage");
    REQUIRE(e0["name"] == "Q1");
    REQUIRE(e1["@type"] == "Event");
    REQUIRE(e1["name"] == "E1");
}

TEST_CASE("seo_schema: BreadcrumbList for nested page includes current as last",
          "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("Hello", "/posts/hello/", "2025-01-01");
    nlohmann::json pages = nlohmann::json::array({
        make_page("Home", "/", "2025-01-01"),
        make_page("Posts", "/posts/", "2025-01-01"),
        page,
    });
    std::string out = build_json_ld(cfg, page, pages);
    // WebSite + WebPage(page) + Breadcrumb = 3
    REQUIRE(count_scripts(out) == 3);
    nlohmann::json bl = extract_script(out, 2);
    REQUIRE(bl["@type"] == "BreadcrumbList");
    auto items = bl["itemListElement"];
    REQUIRE(items.size() == 3);
    REQUIRE(items[0]["position"] == 1);
    REQUIRE(items[0]["name"] == "My Site");    // root → site title
    REQUIRE(items[0]["item"] == "https://example.com/");
    REQUIRE(items[1]["name"] == "Posts");      // resolved via pages_array
    REQUIRE(items[2]["position"] == 3);
    REQUIRE(items[2]["name"] == "Hello");      // current page last
    REQUIRE(items[2]["item"] == "https://example.com/posts/hello/");
}

TEST_CASE("seo_schema: Product maps price/currency to Offer", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("Widget", "/products/widget/", "2025-01-01");
    page["type"] = "Product";
    page["brand"] = "Acme";
    page["price"] = 19.99;
    page["currency"] = "USD";
    page["availability"] = "https://schema.org/InStock";
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json p = extract_script(out, 1);
    REQUIRE(p["@type"] == "Product");
    REQUIRE(p["name"] == "Widget");
    REQUIRE(p["brand"]["@type"] == "Brand");
    REQUIRE(p["brand"]["name"] == "Acme");
    REQUIRE(p["offers"]["@type"] == "Offer");
    REQUIRE(p["offers"]["price"] == 19.99);
    REQUIRE(p["offers"]["priceCurrency"] == "USD");
    REQUIRE(p["offers"]["availability"] == "https://schema.org/InStock");
}

TEST_CASE("seo_schema: SoftwareApplication maps category/os", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("MyApp", "/apps/myapp/", "2025-01-01");
    page["type"] = "SoftwareApplication";
    page["application_category"] = "DeveloperApplication";
    page["operating_system"] = "macOS";
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json p = extract_script(out, 1);
    REQUIRE(p["@type"] == "SoftwareApplication");
    REQUIRE(p["applicationCategory"] == "DeveloperApplication");
    REQUIRE(p["operatingSystem"] == "macOS");
}

TEST_CASE("seo_schema: image resolved against base_url; canonical used when set",
          "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("Hello", "/posts/hello/", "2025-01-01");
    page["image"] = "/img/cover.png";
    page["canonical"] = "https://canonical.example/elsewhere";
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json p = extract_script(out, 1);
    REQUIRE(p["image"] == "https://example.com/img/cover.png");
    REQUIRE(p["mainEntityOfPage"]["@id"] == "https://canonical.example/elsewhere");
    // Script tags are well-formed.
    REQUIRE(contains(out, "<script type=\"application/ld+json\">"));
    REQUIRE(contains(out, "</script>"));
}

TEST_CASE("seo_schema: validate flags missing BlogPosting fields", "[seo_schema]") {
    nlohmann::json s;
    s["@type"] = "BlogPosting";
    auto issues = validate(s, "/posts/x/");
    REQUIRE(issues.size() == 3);
    // Partial — only datePublished missing.
    nlohmann::json s2;
    s2["@type"] = "BlogPosting";
    s2["headline"] = "Hi";
    s2["author"] = nlohmann::json{{"@type", "Person"}, {"name", "X"}};
    auto issues2 = validate(s2, "/posts/x/");
    REQUIRE(issues2.size() == 1);
    REQUIRE(issues2[0].field == "datePublished");
}

TEST_CASE("seo_schema: validate passes for complete WebPage", "[seo_schema]") {
    nlohmann::json s;
    s["@type"] = "WebPage";
    s["name"] = "Home";
    REQUIRE(validate(s, "/").empty());
}

TEST_CASE("seo_schema: validate Product requires offers.price", "[seo_schema]") {
    nlohmann::json s;
    s["@type"] = "Product";
    s["name"] = "Widget";
    auto issues = validate(s, "/p/");
    REQUIRE(issues.size() == 1);
    REQUIRE(issues[0].field == "offers.price");
}

TEST_CASE("seo_schema: validate SoftwareApplication requires applicationCategory",
          "[seo_schema]") {
    nlohmann::json s;
    s["@type"] = "SoftwareApplication";
    s["name"] = "App";
    auto issues = validate(s, "/a/");
    REQUIRE(issues.size() == 1);
    REQUIRE(issues[0].field == "applicationCategory");
}

// --- G7: Citation meta tags ---

TEST_CASE("seo_schema: citation tags disabled returns empty", "[seo_schema]") {
    Config cfg = base_config();
    cfg.citation_tags_enabled = false;
    auto page = make_page("Hello", "/posts/hello/", "2025-06-01");
    REQUIRE(build_citation_tags(cfg, page).empty());
}

TEST_CASE("seo_schema: citation tags all present", "[seo_schema]") {
    Config cfg = base_config();
    cfg.citation_tags_enabled = true;

    nlohmann::json page;
    page["title"] = "Deep Learning";
    page["url"]   = "/posts/dl/";
    page["date"]  = "2025-06-01";
    page["description"] = "An abstract.";
    page["author"] = "Jane Doe";
    page["pdf_url"] = "https://example.com/paper.pdf";
    page["journal"] = "Journal of ML";
    page["doi"]     = "10.1000/xyz123";
    page["tags"]    = {"machine-learning", "neural-networks"};

    std::string html = build_citation_tags(cfg, page);
    REQUIRE(contains(html, "name=\"citation_title\" content=\"Deep Learning\""));
    REQUIRE(contains(html, "name=\"citation_publication_date\" content=\"2025-06-01\""));
    REQUIRE(contains(html, "name=\"citation_online_date\" content=\"2025-06-01\""));
    REQUIRE(contains(html, "name=\"citation_author\" content=\"Jane Doe\""));
    REQUIRE(contains(html, "name=\"citation_pdf_url\" content=\"https://example.com/paper.pdf\""));
    REQUIRE(contains(html, "name=\"citation_abstract\" content=\"An abstract.\""));
    REQUIRE(contains(html, "name=\"citation_journal_title\" content=\"Journal of ML\""));
    REQUIRE(contains(html, "name=\"citation_doi\" content=\"10.1000/xyz123\""));
    REQUIRE(contains(html, "name=\"citation_keywords\" content=\"machine-learning; neural-networks\""));
}

TEST_CASE("seo_schema: citation tags omit missing fields", "[seo_schema]") {
    Config cfg = base_config();
    cfg.citation_tags_enabled = true;

    nlohmann::json page;
    page["title"] = "Minimal";
    page["url"]   = "/posts/minimal/";
    // No date, author, pdf_url, journal, doi, or tags.

    std::string html = build_citation_tags(cfg, page);
    REQUIRE(contains(html, "name=\"citation_title\" content=\"Minimal\""));
    REQUIRE(!contains(html, "citation_author"));
    REQUIRE(!contains(html, "citation_publication_date"));
    REQUIRE(!contains(html, "citation_online_date"));
    REQUIRE(!contains(html, "citation_pdf_url"));
    REQUIRE(!contains(html, "citation_abstract"));
    REQUIRE(!contains(html, "citation_journal_title"));
    REQUIRE(!contains(html, "citation_doi"));
    REQUIRE(!contains(html, "citation_keywords"));
}

TEST_CASE("seo_schema: citation author as resolved Person object", "[seo_schema]") {
    Config cfg = base_config();
    cfg.citation_tags_enabled = true;

    nlohmann::json page;
    page["title"] = "Post";
    page["url"]   = "/posts/post/";
    page["date"]  = "2025-06-01";
    nlohmann::json person;
    person["@type"] = "Person";
    person["name"]  = "John Smith";
    page["author"]  = person;

    std::string html = build_citation_tags(cfg, page);
    REQUIRE(contains(html, "name=\"citation_author\" content=\"John Smith\""));
}

TEST_CASE("seo_schema: citation keywords semicolon-joined", "[seo_schema]") {
    Config cfg = base_config();
    cfg.citation_tags_enabled = true;

    nlohmann::json page;
    page["title"] = "T";
    page["url"]   = "/posts/t/";
    page["tags"]  = {"alpha", "beta", "gamma"};

    std::string html = build_citation_tags(cfg, page);
    REQUIRE(contains(html, "citation_keywords\" content=\"alpha; beta; gamma\""));
}

TEST_CASE("seo_schema: citation abstract prefers tldr", "[seo_schema]") {
    Config cfg = base_config();
    cfg.citation_tags_enabled = true;

    nlohmann::json page;
    page["title"] = "T";
    page["url"]   = "/posts/t/";
    page["description"] = "Long description.";
    page["tldr"] = "Short summary.";

    std::string html = build_citation_tags(cfg, page);
    REQUIRE(contains(html, "citation_abstract\" content=\"Short summary.\""));
    REQUIRE(!contains(html, "Long description."));
}

TEST_CASE("seo_schema: citation online date prefers created", "[seo_schema]") {
    Config cfg = base_config();
    cfg.citation_tags_enabled = true;

    nlohmann::json page;
    page["title"] = "T";
    page["url"]   = "/posts/t/";
    page["date"]  = "2025-06-01";
    page["created"] = "2025-05-15";

    std::string html = build_citation_tags(cfg, page);
    REQUIRE(contains(html, "citation_online_date\" content=\"2025-05-15\""));
    REQUIRE(contains(html, "citation_publication_date\" content=\"2025-06-01\""));
}

TEST_CASE("seo_schema: passage index emits hasPart on page schema", "[seo_schema]") {
    Config cfg = base_config();
    nlohmann::json page = make_page("Hello", "/posts/hello/", "2025-01-01");
    page["passages"] = nlohmann::json::array({
        {{"id", "intro"}, {"heading", "Intro"}, {"text", "First bit."}, {"level", 2}},
        {{"id", "details"}, {"heading", "Details"}, {"text", "Second bit."}, {"level", 3}},
    });

    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    // Page schema is script index 1 (after the always-on WebSite schema).
    nlohmann::json s = extract_script(out, 1);
    REQUIRE(s["@type"] == "BlogPosting");
    REQUIRE(s.contains("hasPart"));
    REQUIRE(s["hasPart"].size() == 2);
    REQUIRE(s["hasPart"][0]["@type"] == "WebPageElement");
    REQUIRE(s["hasPart"][0]["name"] == "Intro");
    REQUIRE(s["hasPart"][0]["text"] == "First bit.");
    REQUIRE(s["hasPart"][0]["url"] == "https://example.com/posts/hello/#intro");
    REQUIRE(s["hasPart"][1]["url"] == "https://example.com/posts/hello/#details");
}

TEST_CASE("seo_schema: hasPart respects canonical URL when set", "[seo_schema]") {
    Config cfg = base_config();
    nlohmann::json page = make_page("P", "/posts/p/", "2025-01-01");
    page["canonical"] = "https://canonical.example.com/post/";
    page["passages"] = nlohmann::json::array({
        {{"id", "sec"}, {"heading", "Sec"}, {"text", "Body."}, {"level", 2}},
    });

    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json s = extract_script(out, 1);
    REQUIRE(s["hasPart"][0]["url"] == "https://canonical.example.com/post/#sec");
}

TEST_CASE("seo_schema: explicit schema.hasPart overrides auto-generated", "[seo_schema]") {
    Config cfg = base_config();
    nlohmann::json page = make_page("P", "/p/", "2025-01-01");
    page["passages"] = nlohmann::json::array({
        {{"id", "auto"}, {"heading", "Auto"}, {"text", "Auto body."}, {"level", 2}},
    });
    // Explicit schema.hasPart should win (deep_merge replaces arrays).
    page["schema"] = nlohmann::json::object();
    page["schema"]["hasPart"] = nlohmann::json::array({
        nlohmann::json::object({{"@type", "WebPageElement"}, {"name", "Manual"}}),
    });

    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json s = extract_script(out, 1);
    REQUIRE(s["hasPart"].size() == 1);
    REQUIRE(s["hasPart"][0]["name"] == "Manual");
}

TEST_CASE("seo_schema: no passages -> no hasPart key", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("P", "/p/", "2025-01-01");
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json s = extract_script(out, 1);
    REQUIRE_FALSE(s.contains("hasPart"));
}

// --- G9: TL;DR / Key Takeaways ---

TEST_CASE("seo_schema: tldr overrides description in schema", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("Hello", "/posts/hello/", "2025-01-01");
    page["description"] = "Long description.";
    page["excerpt"] = "Excerpt text.";
    page["tldr"] = "Short summary.";
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json s = extract_script(out, 1);
    REQUIRE(s["description"] == "Short summary.");
}

TEST_CASE("seo_schema: description used when no tldr", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("Hello", "/posts/hello/", "2025-01-01");
    page["description"] = "The description.";
    page["excerpt"] = "The excerpt.";
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json s = extract_script(out, 1);
    REQUIRE(s["description"] == "The description.");
}

TEST_CASE("seo_schema: key_takeaways populate mainEntity ItemList", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("Hello", "/posts/hello/", "2025-01-01");
    page["key_takeaways"] = {"First point", "Second point", "Third point"};
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json s = extract_script(out, 1);
    REQUIRE(s.contains("mainEntity"));
    REQUIRE(s["mainEntity"]["@type"] == "ItemList");
    REQUIRE(s["mainEntity"]["itemListElement"].size() == 3);
    REQUIRE(s["mainEntity"]["itemListElement"][0]["@type"] == "ListItem");
    REQUIRE(s["mainEntity"]["itemListElement"][0]["position"] == 1);
    REQUIRE(s["mainEntity"]["itemListElement"][0]["name"] == "First point");
    REQUIRE(s["mainEntity"]["itemListElement"][2]["position"] == 3);
    REQUIRE(s["mainEntity"]["itemListElement"][2]["name"] == "Third point");
}

TEST_CASE("seo_schema: no key_takeaways -> no mainEntity key", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("P", "/p/", "2025-01-01");
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json s = extract_script(out, 1);
    REQUIRE_FALSE(s.contains("mainEntity"));
}

TEST_CASE("seo_schema: explicit schema.mainEntity overrides key_takeaways",
          "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("P", "/p/", "2025-01-01");
    page["key_takeaways"] = {"Auto point"};
    page["schema"] = nlohmann::json::object();
    page["schema"]["mainEntity"] = nlohmann::json{{"@type", "Thing"}, {"name", "Manual"}};
    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json s = extract_script(out, 1);
    REQUIRE(s["mainEntity"]["@type"] == "Thing");
    REQUIRE(s["mainEntity"]["name"] == "Manual");
}

// --- G10: Brand Mention Normalization ---

TEST_CASE("seo_schema: validate_organization empty when org_name unset", "[seo_schema]") {
    Config cfg = base_config();
    REQUIRE(validate_organization(cfg, {}).empty());
}

TEST_CASE("seo_schema: validate_organization clean with matching name + URL logo",
          "[seo_schema]") {
    Config cfg = base_config();
    cfg.org_name = "My Site";  // matches site_title
    cfg.org_logo = "https://example.com/logo.png";  // absolute URL → no file check
    cfg.org_same_as = {"https://twitter.com/mysite", "https://github.com/mysite"};
    cfg.org_founders = {"alice"};
    REQUIRE(validate_organization(cfg, {"alice", "bob"}).empty());
}

TEST_CASE("seo_schema: validate_organization warns org_name differs from site_title",
          "[seo_schema]") {
    Config cfg = base_config();
    cfg.org_name = "Acme Inc";
    cfg.org_logo = "https://example.com/logo.png";
    auto issues = validate_organization(cfg, {});
    REQUIRE(issues.size() == 1);
    REQUIRE(issues[0].field == "org_name");
    REQUIRE(contains(issues[0].message, "Acme Inc"));
    REQUIRE(contains(issues[0].message, "My Site"));
}

TEST_CASE("seo_schema: validate_organization warns org_logo file not found",
          "[seo_schema]") {
    Config cfg = base_config();
    cfg.org_name = "My Site";
    cfg.static_dir = "/nonexistent_static_dir";
    cfg.org_logo = "/missing-logo.png";
    auto issues = validate_organization(cfg, {});
    bool found = false;
    for (const auto& i : issues) {
        if (i.field == "org_logo") {
            REQUIRE(contains(i.message, "file not found"));
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("seo_schema: validate_organization warns same_as not URL", "[seo_schema]") {
    Config cfg = base_config();
    cfg.org_name = "My Site";
    cfg.org_logo = "https://example.com/logo.png";
    cfg.org_same_as = {"https://valid.com", "not-a-url", "also-bad"};
    auto issues = validate_organization(cfg, {});
    int count = 0;
    for (const auto& i : issues) {
        if (i.field.find("org_same_as") == 0) {
            REQUIRE(contains(i.message, "not a valid URL"));
            ++count;
        }
    }
    REQUIRE(count == 2);  // two bad entries
}

TEST_CASE("seo_schema: validate_organization warns founders not known slugs",
          "[seo_schema]") {
    Config cfg = base_config();
    cfg.org_name = "My Site";
    cfg.org_logo = "https://example.com/logo.png";
    cfg.org_founders = {"alice", "charlie"};  // alice known, charlie not
    auto issues = validate_organization(cfg, {"alice", "bob"});
    bool found = false;
    for (const auto& i : issues) {
        if (i.field == "org_founders" && contains(i.message, "charlie")) {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("seo_schema: validate_organization skips founders check when no authors",
          "[seo_schema]") {
    Config cfg = base_config();
    cfg.org_name = "My Site";
    cfg.org_logo = "https://example.com/logo.png";
    cfg.org_founders = {"unknown-person"};
    // Empty known_author_slugs → founders check is skipped entirely.
    REQUIRE(validate_organization(cfg, {}).empty());
}

TEST_CASE("seo_schema: build_org_context empty when org_name unset", "[seo_schema]") {
    Config cfg = base_config();
    nlohmann::json org = build_org_context(cfg);
    REQUIRE(org.is_object());
    REQUIRE(org.empty());
}

TEST_CASE("seo_schema: build_org_context has all fields", "[seo_schema]") {
    Config cfg = base_config();
    cfg.org_name = "Acme Inc";
    cfg.org_legal_name = "Acme Incorporated";
    cfg.org_logo = "/logo.png";
    cfg.org_founding_date = "2001-02-03";
    cfg.org_founders = {"Alice", "Bob"};
    cfg.org_same_as = {"https://twitter.com/acme"};
    cfg.org_url = "https://acme.example.com";

    nlohmann::json org = build_org_context(cfg);
    REQUIRE(org["name"] == "Acme Inc");
    REQUIRE(org["legal_name"] == "Acme Incorporated");
    REQUIRE(org["logo_url"] == "https://example.com/logo.png");
    REQUIRE(org["founding_date"] == "2001-02-03");
    REQUIRE(org["url"] == "https://acme.example.com");
    REQUIRE(org["founders"].size() == 2);
    REQUIRE(org["founders"][0] == "Alice");
    REQUIRE(org["same_as"][0] == "https://twitter.com/acme");
}

TEST_CASE("seo_schema: build_org_context url defaults to base_url", "[seo_schema]") {
    Config cfg = base_config();
    cfg.org_name = "My Site";
    nlohmann::json org = build_org_context(cfg);
    REQUIRE(org["url"] == "https://example.com");  // falls back to site_base_url
}

// --- G12: readability (wordCount + timeRequired) on JSON-LD ---

TEST_CASE("seo_schema: BlogPosting emits wordCount and timeRequired", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("My Post", "/posts/my-post/", "2025-01-01");
    page["word_count"]   = 350;
    page["reading_time"] = 2;

    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    // No org_name -> script 0 is WebSite, script 1 is the page schema.
    nlohmann::json schema = extract_script(out, 1);
    REQUIRE(schema["@type"] == "BlogPosting");
    REQUIRE(schema["wordCount"] == 350);
    REQUIRE(schema["timeRequired"] == "PT2M");
}

TEST_CASE("seo_schema: WebPage omits wordCount but emits timeRequired", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("About", "/about/", "2025-01-01");
    page["word_count"]   = 120;
    page["reading_time"] = 1;

    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json schema = extract_script(out, 1);
    REQUIRE(schema["@type"] == "WebPage");
    REQUIRE_FALSE(schema.contains("wordCount"));  // wordCount only on Article types
    REQUIRE(schema["timeRequired"] == "PT1M");     // timeRequired on any page
}

TEST_CASE("seo_schema: zero reading_time omits timeRequired", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("Empty", "/posts/empty/", "2025-01-01");
    page["word_count"]   = 0;
    page["reading_time"] = 0;

    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json schema = extract_script(out, 1);
    REQUIRE_FALSE(schema.contains("wordCount"));
    REQUIRE_FALSE(schema.contains("timeRequired"));
}

TEST_CASE("seo_schema: explicit page.schema.wordCount overrides auto", "[seo_schema]") {
    Config cfg = base_config();
    auto page = make_page("Post", "/posts/post/", "2025-01-01");
    page["word_count"]   = 300;
    page["reading_time"] = 2;
    // An explicit schema override should win via deep_merge.
    page["schema"] = {
        {"@type", "BlogPosting"},
        {"wordCount", 999}
    };

    std::string out = build_json_ld(cfg, page, nlohmann::json::array());
    nlohmann::json schema = extract_script(out, 1);
    REQUIRE(schema["wordCount"] == 999);
    // timeRequired wasn't overridden — auto value still present.
    REQUIRE(schema["timeRequired"] == "PT2M");
}
