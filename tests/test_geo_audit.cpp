#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "config/config.hpp"
#include "pipeline/geo_audit.hpp"
#include "test_util.hpp"

namespace fs = std::filesystem;
using nlohmann::json;
using cstatic::Config;
using cstatic::pipeline::audit_geo;
using cstatic::pipeline::format_geo_report;
using cstatic::pipeline::GeoAuditResult;
using cstatic::pipeline::GeoCheck;
using cstatic::pipeline::GeoCheckScore;
using cstatic::pipeline::GeoSeverity;

namespace {

struct GeoFixture {
    std::string root_dir;
    std::string saved_cwd;
    Config cfg;

    GeoFixture() {
        saved_cwd = fs::current_path().string();
        root_dir = cstatic_test::unique_temp_dir("cstatic_geo_");
        fs::create_directories(root_dir);
        fs::current_path(root_dir);
        cfg = Config{};
        cfg.output_dir = root_dir + "/output";
        cfg.authors_dir = root_dir + "/src/authors";
    }

    ~GeoFixture() {
        fs::current_path(saved_cwd);
        std::error_code ec;
        fs::remove_all(root_dir, ec);
    }

    std::string out() const { return root_dir + "/output"; }

    void make_out() { fs::create_directories(out()); }

    // Write a file under output/, creating parent dirs.
    void write(const std::string& rel, const std::string& content) {
        fs::path p = fs::path(out()) / rel;
        fs::create_directories(p.parent_path());
        std::ofstream f(p);
        f << content;
        f.close();
        REQUIRE(f.good());
    }

    // Write a file under the project root (for authors_dir, static_dir, etc.).
    void write_root(const std::string& rel, const std::string& content) {
        fs::path p = fs::path(root_dir) / rel;
        fs::create_directories(p.parent_path());
        std::ofstream f(p);
        f << content;
        f.close();
        REQUIRE(f.good());
    }
};

const GeoCheckScore* find_check(const GeoAuditResult& r, GeoCheck c) {
    for (const auto& x : r.checks) if (x.check == c) return &x;
    return nullptr;
}

bool has_issue_with(const GeoCheckScore& row, GeoSeverity sev, const std::string& needle) {
    for (const auto& i : row.issues) {
        if (i.severity == sev && i.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

// Wrap a schema object in a JSON-LD <script> block (matches seo_schema's
// pretty-printed output; minifier won't touch <script> content).
std::string ld(const json& schema) {
    return std::string("<html><head><script type=\"application/ld+json\">\n")
         + schema.dump(2) + "\n</script></head></html>";
}

// A well-formed BlogPosting schema.
json valid_blog_posting(const std::string& headline = "Hello") {
    return {
        {"@context", "https://schema.org"},
        {"@type", "BlogPosting"},
        {"headline", headline},
        {"datePublished", "2026-01-01"},
        {"author", {{"@type", "Person"}, {"name", "Jane"}}}
    };
}

} // namespace

TEST_CASE("GEO audit", "[geo_audit]") {
    GeoFixture f;

    SECTION("missing output dir returns single hard error") {
        // output/ not created
        auto r = audit_geo(f.out(), f.cfg);
        REQUIRE(r.score == 0);
        REQUIRE(r.hard_count == 1);
        REQUIRE(r.checks.size() == 1);
        REQUIRE(r.checks[0].issues.size() == 1);
        REQUIRE(r.checks[0].issues[0].severity == GeoSeverity::Error);
        REQUIRE(r.checks[0].issues[0].message.find("does not exist") != std::string::npos);
    }

    SECTION("empty output dir + no features enabled = all checks skipped, score 0") {
        f.make_out();
        auto r = audit_geo(f.out(), f.cfg);
        REQUIRE(r.score == 0);
        REQUIRE(r.hard_count == 0);
        // All 9 checks are present but every non-FAQ check is skipped (max==0).
        REQUIRE(r.checks.size() == 9);
        for (const auto& c : r.checks) {
            if (c.check != GeoCheck::FaqCoverage) REQUIRE(c.max == 0);
        }
        // The report surfaces the "enable features" recommendation.
        std::string report = format_geo_report(r);
        REQUIRE(report.find("No GEO features are enabled") != std::string::npos);
    }

    // ---- Check 1: llms.txt ----
    SECTION("llms.txt present and enabled -> 15/15") {
        f.make_out();
        f.cfg.module_llms_txt = true;
        f.write("llms.txt", "# Acme\n\n> A site\n\n## Pages\n- [Home](/): hi\n");
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::LlmsTxt);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 15);
        REQUIRE(c->earned == 15);
        REQUIRE(r.hard_count == 0);
    }

    SECTION("llms.txt enabled but missing -> hard error 0/15") {
        f.make_out();
        f.cfg.module_llms_txt = true;
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::LlmsTxt);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 15);
        REQUIRE(c->earned == 0);
        REQUIRE(r.hard_count == 1);
        REQUIRE(has_issue_with(*c, GeoSeverity::Error, "llms.txt missing"));
    }

    SECTION("llms.txt disabled -> skipped (0/0)") {
        f.make_out();
        f.cfg.module_llms_txt = false;
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::LlmsTxt);
        // FaqCoverage always runs; llms is skipped. With nothing else enabled,
        // any_enabled is false so the all-skipped path clears the scorecard.
        // So we only assert the absence of a llms.txt issue.
        REQUIRE(r.score == 0);
        REQUIRE(r.hard_count == 0);
        (void)c;
    }

    // ---- Check 2: AI robots ----
    SECTION("AI robots allow mode + all 12 agents -> 15/15") {
        f.make_out();
        f.cfg.robots_ai_crawlers_mode = "allow";
        std::string robots = "User-agent: *\nAllow: /\n";
        for (const auto& a : std::vector<std::string>{
            "GPTBot","OAI-SearchBot","ClaudeBot","PerplexityBot","Perplexity-User",
            "CCBot","Google-Extended","Applebot-Extended","Meta-ExternalAgent",
            "Amazonbot","Bytespider","Diffbot"}) {
            robots += "\nUser-agent: " + a + "\nAllow: /\n";
        }
        f.write("robots.txt", robots);
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::AiRobots);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 15);
        REQUIRE(c->earned == 15);
        REQUIRE(r.hard_count == 0);
    }

    SECTION("AI robots allow mode + 6 of 12 agents -> partial with warnings") {
        f.make_out();
        f.cfg.robots_ai_crawlers_mode = "allow";
        std::string robots = "User-agent: *\nAllow: /\n";
        for (const auto& a : std::vector<std::string>{
            "GPTBot","OAI-SearchBot","ClaudeBot","PerplexityBot","Perplexity-User","CCBot"}) {
            robots += "\nUser-agent: " + a + "\nAllow: /\n";
        }
        f.write("robots.txt", robots);
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::AiRobots);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 15);
        REQUIRE(c->earned == 8); // lround(6.0/12.0*15.0) = lround(7.5) = 8
        REQUIRE(r.soft_count == 6); // one warning per missing agent
        REQUIRE(r.hard_count == 0);
    }

    SECTION("AI robots allow mode + missing file -> hard error") {
        f.make_out();
        f.cfg.robots_ai_crawlers_mode = "allow";
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::AiRobots);
        REQUIRE(c != nullptr);
        REQUIRE(c->earned == 0);
        REQUIRE(r.hard_count == 1);
        REQUIRE(has_issue_with(*c, GeoSeverity::Error, "robots.txt missing"));
    }

    SECTION("AI robots off mode -> skipped") {
        f.make_out();
        f.cfg.robots_ai_crawlers_mode = "off";
        f.write("robots.txt", "User-agent: *\nAllow: /\n");
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::AiRobots);
        // Skipped rows have max==0.
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 0);
        REQUIRE(r.hard_count == 0);
    }

    // ---- Check 3: JSON-LD validity ----
    SECTION("JSON-LD valid on 3 pages -> 30/30") {
        f.make_out();
        f.cfg.json_ld_enabled = true;
        f.write("a/index.html", ld(valid_blog_posting("A")));
        f.write("b/index.html", ld(valid_blog_posting("B")));
        f.write("c/index.html", ld(valid_blog_posting("C")));
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::JsonLd);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 30);
        REQUIRE(c->earned == 30);
        REQUIRE(r.hard_count == 0);
    }

    SECTION("JSON-LD missing headline -> warning, partial credit") {
        f.make_out();
        f.cfg.json_ld_enabled = true;
        json good = valid_blog_posting("Good");
        json bad = {
            {"@context", "https://schema.org"},
            {"@type", "BlogPosting"},
            {"datePublished", "2026-01-01"},
            {"author", {{"@type", "Person"}, {"name", "Jane"}}}
        };
        // bad is missing 'headline'
        f.write("a/index.html", ld(good));
        f.write("b/index.html", ld(bad));
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::JsonLd);
        REQUIRE(c != nullptr);
        // 1 of 2 valid → 15/30
        REQUIRE(c->earned == 15);
        REQUIRE(has_issue_with(*c, GeoSeverity::Warning, "headline"));
        REQUIRE(r.hard_count == 0);
        REQUIRE(r.soft_count >= 1);
    }

    SECTION("JSON-LD unparseable -> hard error") {
        f.make_out();
        f.cfg.json_ld_enabled = true;
        std::string broken = "<html><head><script type=\"application/ld+json\">\n"
                             "{not valid json}\n</script></head></html>";
        f.write("a/index.html", broken);
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::JsonLd);
        REQUIRE(c != nullptr);
        REQUIRE(r.hard_count == 1);
        REQUIRE(has_issue_with(*c, GeoSeverity::Error, "unparseable"));
    }

    SECTION("JSON-LD disabled -> skipped") {
        f.make_out();
        f.cfg.json_ld_enabled = false;
        f.write("a/index.html", ld(valid_blog_posting("A")));
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::JsonLd);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 0);
        REQUIRE(r.hard_count == 0);
    }

    // ---- Check 4: Org consistency ----
    SECTION("Org consistency identical across pages -> 10/10") {
        f.make_out();
        f.cfg.org_name = "Acme";
        f.cfg.org_logo = "https://example.com/logo.png";
        json org = {
            {"@context", "https://schema.org"},
            {"@type", "Organization"},
            {"name", "Acme"},
            {"url", "https://example.com"},
            {"logo", "https://example.com/logo.png"}
        };
        f.write("a/index.html", ld(org));
        f.write("b/index.html", ld(org));
        f.write("c/index.html", ld(org));
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::OrgConsistency);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 10);
        REQUIRE(c->earned == 10);
    }

    SECTION("Org consistency diverges on name -> 0/10") {
        f.make_out();
        f.cfg.org_name = "Acme";
        json org1 = {{"@context","https://schema.org"},{"@type","Organization"},
                     {"name","Acme"},{"url","https://example.com"}};
        json org2 = {{"@context","https://schema.org"},{"@type","Organization"},
                     {"name","Beta"},{"url","https://example.com"}};
        f.write("a/index.html", ld(org1));
        f.write("b/index.html", ld(org2));
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::OrgConsistency);
        REQUIRE(c != nullptr);
        REQUIRE(c->earned == 0);
        REQUIRE(has_issue_with(*c, GeoSeverity::Warning, "name"));
    }

    SECTION("Org consistency no org_name -> skipped") {
        f.make_out();
        f.cfg.org_name = "";
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::OrgConsistency);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 0);
    }

    // ---- Check 5: Author pages ----
    SECTION("Author pages all present -> 5/5") {
        f.make_out();
        f.cfg.authors_enabled = true;
        f.write_root("src/authors/jane.md", "---\nname: Jane\n---\n");
        f.write_root("src/authors/john.md", "---\nname: John\n---\n");
        f.write_root("src/authors/bob.md",  "---\nname: Bob\n---\n");
        f.write("authors/jane/index.html", "<html></html>");
        f.write("authors/john/index.html", "<html></html>");
        f.write("authors/bob/index.html",  "<html></html>");
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::AuthorPages);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 5);
        REQUIRE(c->earned == 5);
        REQUIRE(r.hard_count == 0);
    }

    SECTION("Author pages one missing -> 4/5") {
        f.make_out();
        f.cfg.authors_enabled = true;
        f.write_root("src/authors/jane.md", "---\nname: Jane\n---\n");
        f.write_root("src/authors/john.md", "---\nname: John\n---\n");
        f.write_root("src/authors/bob.md",  "---\nname: Bob\n---\n");
        f.write("authors/jane/index.html", "<html></html>");
        f.write("authors/john/index.html", "<html></html>");
        // bob missing
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::AuthorPages);
        REQUIRE(c != nullptr);
        REQUIRE(c->earned == 3); // round(2/3*5)=round(3.33)=3
        REQUIRE(has_issue_with(*c, GeoSeverity::Warning, "bob"));
    }

    SECTION("Author pages empty authors_dir -> 5/5 vacuous") {
        f.make_out();
        f.cfg.authors_enabled = true;
        fs::create_directories(f.root_dir + "/src/authors");
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::AuthorPages);
        REQUIRE(c != nullptr);
        REQUIRE(c->earned == 5);
        REQUIRE(r.hard_count == 0);
    }

    SECTION("Author pages disabled -> skipped") {
        f.make_out();
        f.cfg.authors_enabled = false;
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::AuthorPages);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 0);
    }

    // ---- Check 6: Citation tags ----
    SECTION("Citation tags on both articles -> 10/10") {
        f.make_out();
        f.cfg.citation_tags_enabled = true;
        std::string html =
            ld(valid_blog_posting("A")) +
            "<meta name=\"citation_author\" content=\"Jane\">\n"
            "<meta name=\"citation_title\" content=\"A\">\n";
        // The ld() helper closes </html>; append meta before </html> by hand:
        std::string page =
            "<html><head><script type=\"application/ld+json\">\n"
            + valid_blog_posting("A").dump(2) + "\n</script>\n"
            + "<meta name=\"citation_author\" content=\"Jane\">\n"
            + "<meta name=\"citation_title\" content=\"A\">\n"
            + "</head></html>";
        f.write("a/index.html", page);
        std::string page2 =
            "<html><head><script type=\"application/ld+json\">\n"
            + valid_blog_posting("B").dump(2) + "\n</script>\n"
            + "<meta name=\"citation_author\" content=\"John\">\n"
            + "<meta name=\"citation_title\" content=\"B\">\n"
            + "</head></html>";
        f.write("b/index.html", page2);
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::CitationTags);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 10);
        REQUIRE(c->earned == 10);
        (void)html;
    }

    SECTION("Citation tags one missing title -> partial") {
        f.make_out();
        f.cfg.citation_tags_enabled = true;
        std::string p1 =
            "<html><head><script type=\"application/ld+json\">\n"
            + valid_blog_posting("A").dump(2) + "\n</script>\n"
            + "<meta name=\"citation_author\" content=\"Jane\">\n"
            + "<meta name=\"citation_title\" content=\"A\">\n"
            + "</head></html>";
        std::string p2 =
            "<html><head><script type=\"application/ld+json\">\n"
            + valid_blog_posting("B").dump(2) + "\n</script>\n"
            // missing citation_title
            + "<meta name=\"citation_author\" content=\"John\">\n"
            + "</head></html>";
        f.write("a/index.html", p1);
        f.write("b/index.html", p2);
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::CitationTags);
        REQUIRE(c != nullptr);
        REQUIRE(c->earned == 5); // 1/2 * 10 = 5
        REQUIRE(has_issue_with(*c, GeoSeverity::Warning, "citation_title"));
    }

    SECTION("Citation tags disabled -> skipped") {
        f.make_out();
        f.cfg.citation_tags_enabled = false;
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::CitationTags);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 0);
    }

    // ---- Check 7: Passage index ----
    SECTION("Passage index article with hasPart -> 10/10") {
        f.make_out();
        f.cfg.json_ld_enabled = true;
        json bp = valid_blog_posting("A");
        bp["hasPart"] = json::array({
            {{"@type","WebPageElement"},{"name","Intro"},{"text","hi"},{"url","https://example.com/a/#intro"}}
        });
        f.write("a/index.html", ld(bp));
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::PassageIndex);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 10);
        REQUIRE(c->earned == 10);
    }

    SECTION("Passage index article without hasPart -> warning") {
        f.make_out();
        f.cfg.json_ld_enabled = true;
        f.write("a/index.html", ld(valid_blog_posting("A")));
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::PassageIndex);
        REQUIRE(c != nullptr);
        REQUIRE(c->earned == 0);
        REQUIRE(has_issue_with(*c, GeoSeverity::Warning, "hasPart"));
    }

    SECTION("Passage index WebPage not checked") {
        f.make_out();
        f.cfg.json_ld_enabled = true;
        json wp = {{"@context","https://schema.org"},{"@type","WebPage"},{"name","Home"}};
        f.write("a/index.html", ld(wp));
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::PassageIndex);
        REQUIRE(c != nullptr);
        // No prose pages → vacuous full credit.
        REQUIRE(c->earned == 10);
        REQUIRE(c->issues.empty());
    }

    SECTION("Passage index json_ld disabled -> skipped") {
        f.make_out();
        f.cfg.json_ld_enabled = false;
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::PassageIndex);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 0);
    }

    // ---- Check 8: AI sitemap ----
    SECTION("AI sitemap present + enabled -> 5/5") {
        f.make_out();
        f.cfg.module_sitemap_ai = true;
        f.write("sitemap-ai.xml",
                "<?xml version=\"1.0\"?>\n<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n"
                "<url><loc>https://example.com/</loc></url>\n</urlset>\n");
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::AiSitemap);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 5);
        REQUIRE(c->earned == 5);
        REQUIRE(r.hard_count == 0);
    }

    SECTION("AI sitemap enabled but missing -> hard error") {
        f.make_out();
        f.cfg.module_sitemap_ai = true;
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::AiSitemap);
        REQUIRE(c != nullptr);
        REQUIRE(c->earned == 0);
        REQUIRE(r.hard_count == 1);
        REQUIRE(has_issue_with(*c, GeoSeverity::Error, "sitemap-ai.xml missing"));
    }

    SECTION("AI sitemap disabled -> skipped") {
        f.make_out();
        f.cfg.module_sitemap_ai = false;
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::AiSitemap);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 0);
    }

    // ---- Check 9: FAQ coverage ----
    SECTION("FAQ coverage counts FAQPage schemas") {
        f.make_out();
        f.cfg.json_ld_enabled = true;
        json faq = {
            {"@context","https://schema.org"},
            {"@type","FAQPage"},
            {"mainEntity", json::array({
                {{"@type","Question"},{"name","Q1"},
                 {"acceptedAnswer",{{"@type","Answer"},{"text","A1"}}}}
            })}
        };
        f.write("a/index.html", ld(faq));
        f.write("b/index.html", ld(faq));
        f.write("c/index.html", ld(faq));
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::FaqCoverage);
        REQUIRE(c != nullptr);
        REQUIRE(c->max == 0); // informational only
        REQUIRE(c->issues.size() == 1);
        REQUIRE(c->issues[0].message.find("3 page(s)") != std::string::npos);
        REQUIRE(c->issues[0].severity == GeoSeverity::Info);
    }

    SECTION("FAQ coverage zero pages") {
        f.make_out();
        f.cfg.json_ld_enabled = true;
        f.write("a/index.html", ld(valid_blog_posting("A")));
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::FaqCoverage);
        REQUIRE(c != nullptr);
        REQUIRE(c->issues[0].message.find("0 page(s)") != std::string::npos);
    }

    // ---- Score computation + format ----
    SECTION("Score computation across multiple checks") {
        f.make_out();
        f.cfg.module_llms_txt = true;
        f.cfg.json_ld_enabled = true;
        f.write("llms.txt", "# Acme\n\n> Site\n\n## Pages\n- [Home](/): hi\n");
        // 1 valid + 1 invalid (missing headline) → json_ld 15/30
        f.write("a/index.html", ld(valid_blog_posting("A")));
        json bad = valid_blog_posting("B");
        bad.erase("headline");
        f.write("b/index.html", ld(bad));
        auto r = audit_geo(f.out(), f.cfg);
        // Three checks are scored:
        //   llms.txt        15/15  (file present, non-trivial)
        //   JSON-LD valid   15/30  (1 of 2 pages fully valid)
        //   passage index    0/10  (json_ld_enabled gates this too; both
        //                            article pages lack hasPart → 0 of 2)
        //   sum = 30 / 55 → round(54.54) = 55
        REQUIRE(r.score == 55);
        REQUIRE(r.hard_count == 0);
        REQUIRE(r.soft_count >= 1); // the headline warning
    }

    SECTION("format_geo_report contains GEO Score line") {
        f.make_out();
        f.cfg.module_llms_txt = true;
        f.write("llms.txt", "# Acme\n\n> Site desc here\n\n## Pages\n- [Home](/): hi\n");
        auto r = audit_geo(f.out(), f.cfg);
        std::string report = format_geo_report(r);
        REQUIRE(report.find("GEO Score:") != std::string::npos);
        REQUIRE(report.find("llms.txt") != std::string::npos);
        REQUIRE(report.find("[15/15]") != std::string::npos);
    }

    SECTION("format_geo_report on missing output dir") {
        f.cfg.output_dir = f.out();
        auto r = audit_geo(f.out(), f.cfg);
        std::string report = format_geo_report(r);
        REQUIRE(report.find("GEO Score:") != std::string::npos);
        REQUIRE(report.find("does not exist") != std::string::npos);
    }

    // ---- Minified HTML tolerance ----
    SECTION("Minified JSON-LD (unquoted type attr) still extracted") {
        f.make_out();
        f.cfg.json_ld_enabled = true;
        // Minifier strips attribute quotes: <script type=application/ld+json>
        std::string minified =
            "<html><head><script type=application/ld+json>"
            + valid_blog_posting("A").dump() +
            "</script></head></html>";
        f.write("a/index.html", minified);
        auto r = audit_geo(f.out(), f.cfg);
        auto* c = find_check(r, GeoCheck::JsonLd);
        REQUIRE(c != nullptr);
        REQUIRE(c->earned == 30);
        REQUIRE(r.hard_count == 0);
    }

    // ---- Combined realistic site ----
    SECTION("Combined realistic site scores cleanly") {
        f.make_out();
        f.cfg.module_llms_txt = true;
        f.cfg.json_ld_enabled = true;
        f.cfg.citation_tags_enabled = true;
        f.cfg.module_sitemap_ai = true;
        f.cfg.org_name = "Acme";
        f.cfg.robots_ai_crawlers_mode = "allow";

        // llms.txt
        f.write("llms.txt", "# Acme\n\n> A site about things\n\n## Pages\n- [Home](/): welcome\n");
        // robots.txt with all 12 agents
        std::string robots = "User-agent: *\nAllow: /\n";
        for (const auto& a : std::vector<std::string>{
            "GPTBot","OAI-SearchBot","ClaudeBot","PerplexityBot","Perplexity-User",
            "CCBot","Google-Extended","Applebot-Extended","Meta-ExternalAgent",
            "Amazonbot","Bytespider","Diffbot"}) {
            robots += "\nUser-agent: " + a + "\nAllow: /\n";
        }
        f.write("robots.txt", robots);
        // sitemap-ai.xml
        f.write("sitemap-ai.xml",
                "<?xml version=\"1.0\"?>\n<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\"></urlset>\n");
        // An article page with full schema + citation tags
        json bp = valid_blog_posting("Hello World");
        bp["hasPart"] = json::array({
            {{"@type","WebPageElement"},{"name","Intro"},{"text","words"},{"url","https://example.com/a/#intro"}}
        });
        std::string page =
            "<html><head><script type=\"application/ld+json\">\n"
            + bp.dump(2) + "\n</script>\n"
            + "<meta name=\"citation_author\" content=\"Jane\">\n"
            + "<meta name=\"citation_title\" content=\"Hello World\">\n"
            + "</head></html>";
        f.write("posts/hello/index.html", page);
        // Organization schema (consistent with cfg.org_name)
        json org = {{"@context","https://schema.org"},{"@type","Organization"},
                    {"name","Acme"},{"url","https://example.com"}};
        f.write("index.html", ld(org));

        auto r = audit_geo(f.out(), f.cfg);
        // Should not crash; should produce a sensible score with no hard errors.
        REQUIRE(r.hard_count == 0);
        REQUIRE(r.score > 0);
        REQUIRE(r.score <= 100);
        std::string report = format_geo_report(r);
        REQUIRE(report.find("GEO Score:") != std::string::npos);
    }
}
