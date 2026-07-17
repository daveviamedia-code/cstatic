#include "pipeline/geo_audit.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "config/config.hpp"
#include "modules/seo_schema.hpp"
#include "utils/file_io.hpp"
#include "utils/terminal.hpp"

namespace cstatic {
namespace pipeline {

namespace fs = std::filesystem;
using nlohmann::json;
namespace seo = cstatic::modules::seo_schema;
namespace utils = cstatic::utils;

namespace {

// The 12 standard AI crawler agents. MUST stay in sync with
// src/modules/robots.cpp's ai_agents() list — that is the canonical source.
// Duplicated here (rather than exported) because robots.cpp's list is
// file-local; keeping a copy avoids widening robots.cpp's API surface. If
// robots.cpp gains/loses an agent, update this list too.
const std::vector<std::string>& k_ai_agents() {
    static const std::vector<std::string> agents = {
        "GPTBot", "OAI-SearchBot", "ClaudeBot", "PerplexityBot",
        "Perplexity-User", "CCBot", "Google-Extended", "Applebot-Extended",
        "Meta-ExternalAgent", "Amazonbot", "Bytespider", "Diffbot"
    };
    return agents;
}

// Per-check weights (sum to 100 when all are enabled).
struct CheckWeights {
    int llms_txt       = 15;
    int ai_robots      = 15;
    int json_ld        = 30;
    int org            = 10;
    int authors        = 5;
    int citation_tags  = 10;
    int passage_index  = 10;
    int ai_sitemap     = 5;
    // faq_coverage is informational only — 0 weight.
};

// Human labels for each check (scorecard left column).
const char* check_label(GeoCheck c) {
    switch (c) {
        case GeoCheck::LlmsTxt:        return "llms.txt";
        case GeoCheck::AiRobots:       return "AI crawler allowlist";
        case GeoCheck::JsonLd:         return "JSON-LD valid";
        case GeoCheck::OrgConsistency: return "Organization schema";
        case GeoCheck::AuthorPages:    return "Author pages";
        case GeoCheck::CitationTags:   return "Citation tags";
        case GeoCheck::PassageIndex:   return "Passage index";
        case GeoCheck::AiSitemap:      return "sitemap-ai.xml";
        case GeoCheck::FaqCoverage:    return "FAQ coverage";
    }
    return "?";
}

// Article-typed @types that count as "prose pages" for citation-tag and
// passage-index checks. Must match seo_schema::validate()'s article set.
bool is_article_type(const std::string& t) {
    return t == "BlogPosting" || t == "Article"
        || t == "NewsArticle" || t == "TechArticle";
}

// One HTML page's extracted audit data. Built in a single walk of output_dir
// so checks 3/4/6/7/9 share parsed JSON-LD without re-reading files.
struct HtmlPage {
    std::string rel_path;                    // output-dir-relative, e.g. "posts/foo/index.html"
    std::vector<json> schemas;               // successfully-parsed JSON-LD blocks
    bool                       had_parse_error      = false;
    std::string                parse_error_snippet; // first ~40 chars of the bad block
    std::set<std::string>      citation_meta_names; // lowercase, e.g. {"author","title"}
};

// Extract the inner JSON text from each
// `<script type="application/ld+json">...</script>` block. Tolerates both
// quoted (`type="..."`) and minified-unquoted (`type=...`) attribute forms.
std::vector<std::string> extract_json_ld_blocks(const std::string& html) {
    static const std::regex re(
        R"RE(<script\s+type\s*=\s*["']?application/ld\+json["']?\s*>([\s\S]*?)</script>)RE",
        std::regex::icase);
    std::vector<std::string> out;
    auto begin = html.cbegin();
    auto end   = html.cend();
    std::smatch m;
    auto pos = begin;
    while (std::regex_search(pos, end, m, re)) {
        out.push_back(m[1].str());
        pos = m[0].second;
    }
    return out;
}

// Collect every `citation_X` name from `<meta name="citation_*">` tags.
std::set<std::string> extract_citation_metas(const std::string& html) {
    static const std::regex re(
        R"RE(<meta\b[^>]*\bname\s*=\s*["']?citation_(\w+))RE",
        std::regex::icase);
    std::set<std::string> out;
    auto begin = html.cbegin();
    auto end   = html.cend();
    std::smatch m;
    auto pos = begin;
    while (std::regex_search(pos, end, m, re)) {
        std::string name = m[1].str();
        // lowercase so membership tests are case-insensitive
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        out.insert(name);
        pos = m[0].second;
    }
    return out;
}

// Walk every .html under output_root, parse JSON-LD blocks, collect citation
// meta names. Errors are per-page, not fatal.
std::vector<HtmlPage> collect_html_pages(const fs::path& output_root) {
    std::vector<HtmlPage> pages;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(output_root, ec);
         it != fs::recursive_directory_iterator(); ) {
        if (ec) { ec.clear(); it.increment(ec); continue; }
        std::error_code rec_ec;
        if (!it->is_regular_file(rec_ec)) { it.increment(ec); continue; }
        if (it->path().extension() != ".html") { it.increment(ec); continue; }

        HtmlPage page;
        std::string abs_path = fs::weakly_canonical(it->path(), ec).string();
        page.rel_path = fs::relative(it->path(), output_root, ec).string();
        if (page.rel_path.empty()) page.rel_path = abs_path;

        std::string html = utils::read_file_or_empty(abs_path);
        for (const auto& block : extract_json_ld_blocks(html)) {
            try {
                page.schemas.push_back(json::parse(block));
            } catch (const json::parse_error& e) {
                page.had_parse_error = true;
                page.parse_error_snippet = e.what();
            }
        }
        page.citation_meta_names = extract_citation_metas(html);
        pages.push_back(std::move(page));
        it.increment(ec);
    }
    return pages;
}

// Return the @type of a schema, handling the (rare) array-of-types form by
// picking the first element. Empty string when not set / not a string.
std::string schema_type(const json& s) {
    if (!s.is_object()) return "";
    if (!s.contains("@type")) return "";
    const json& t = s["@type"];
    if (t.is_string()) return t.get<std::string>();
    if (t.is_array() && !t.empty() && t[0].is_string()) return t[0].get<std::string>();
    return "";
}

// Locate the page-level schema (the one that represents the page itself,
// not the site-wide WebSite/Organization/BreadcrumbList scaffolding).
// Heuristic: the first schema whose @type is NOT in {WebSite, Organization,
// BreadcrumbList}. Falls back to the first schema overall.
const json* find_page_schema(const std::vector<json>& schemas) {
    static const std::set<std::string> site_scaffolding = {
        "WebSite", "Organization", "BreadcrumbList"
    };
    const json* first = nullptr;
    for (const auto& s : schemas) {
        if (!s.is_object()) continue;
        std::string t = schema_type(s);
        if (first == nullptr) first = &s;
        if (!t.empty() && site_scaffolding.find(t) == site_scaffolding.end()) {
            return &s;
        }
    }
    return first;
}

// True if a schema's hasPart is a non-empty array (G8 passage index).
bool has_nonempty_haspart(const json& s) {
    if (!s.is_object()) return false;
    if (!s.contains("hasPart")) return false;
    const json& hp = s["hasPart"];
    return hp.is_array() && !hp.empty();
}

// Round a fractional earned score to an integer clamped to [0, max].
int frac_score(int present, int expected, int max) {
    if (expected <= 0) return max; // vacuous — avoid divide-by-zero; full credit
    double r = static_cast<double>(present) / static_cast<double>(expected)
             * static_cast<double>(max);
    int v = static_cast<int>(std::lround(r));
    if (v < 0) v = 0;
    if (v > max) v = max;
    return v;
}

// Append an issue to a check row + bump the result counters.
void add_issue(GeoCheckScore& row, GeoAuditResult& r,
               GeoSeverity sev, const std::string& file,
               const std::string& message) {
    row.issues.push_back({sev, row.check, file, message});
    if (sev == GeoSeverity::Error)   ++r.hard_count;
    else if (sev == GeoSeverity::Warning) ++r.soft_count;
}

// ---- The 9 checks ----

// Check 1: llms.txt
void check_llms_txt(const fs::path& out, const Config& cfg,
                    GeoAuditResult& r, const CheckWeights& w) {
    GeoCheckScore row{GeoCheck::LlmsTxt, check_label(GeoCheck::LlmsTxt), 0, w.llms_txt, {}};
    if (!cfg.module_llms_txt) {
        row.max = 0; // skipped
        r.checks.push_back(std::move(row));
        return;
    }
    fs::path p = out / "llms.txt";
    if (!fs::exists(p)) {
        add_issue(row, r, GeoSeverity::Error, "llms.txt",
                  "llms.txt missing (module_llms_txt=true)");
    } else {
        std::string txt = utils::read_file_or_empty(p.string());
        // Count non-whitespace characters to judge "non-trivial".
        size_t nonws = 0;
        for (char ch : txt) {
            if (!std::isspace(static_cast<unsigned char>(ch))) ++nonws;
        }
        if (nonws <= 10) {
            add_issue(row, r, GeoSeverity::Error, "llms.txt",
                      "llms.txt is empty or trivial (" + std::to_string(nonws) +
                      " non-whitespace chars)");
        } else {
            row.earned = w.llms_txt;
        }
    }
    r.checks.push_back(std::move(row));
}

// Check 2: AI robots allowlist
void check_ai_robots(const fs::path& out, const Config& cfg,
                     GeoAuditResult& r, const CheckWeights& w) {
    GeoCheckScore row{GeoCheck::AiRobots, check_label(GeoCheck::AiRobots), 0, w.ai_robots, {}};
    const std::string& mode = cfg.robots_ai_crawlers_mode;
    if (mode == "off" || mode.empty()) {
        row.max = 0; // skipped
        r.checks.push_back(std::move(row));
        return;
    }

    fs::path p = out / "robots.txt";
    if (!fs::exists(p)) {
        add_issue(row, r, GeoSeverity::Error, "robots.txt",
                  "robots.txt missing (mode=" + mode + ")");
        r.checks.push_back(std::move(row));
        return;
    }

    std::string txt = utils::read_file_or_empty(p.string());
    // Lowercase the whole file for case-insensitive agent matching.
    std::string lower;
    lower.reserve(txt.size());
    for (unsigned char ch : txt) lower.push_back(static_cast<char>(std::tolower(ch)));

    // Build the set of agent names present (lowercased) by scanning
    // `user-agent: <name>` directives.
    std::set<std::string> present;
    static const std::regex ua_re(R"(user-agent:\s*([^\s\n\r]+))",
                                  std::regex::icase);
    auto begin = lower.cbegin();
    auto end   = lower.cend();
    std::smatch m;
    auto pos = begin;
    while (std::regex_search(pos, end, m, ua_re)) {
        present.insert(m[1].str());
        pos = m[0].second;
    }

    std::vector<std::string> expected;
    if (mode == "custom") {
        expected = cfg.robots_ai_crawlers_custom;
    } else { // "allow" or "disallow"
        expected = k_ai_agents();
    }

    if (expected.empty()) {
        // Nothing configured to look for — vacuous full credit.
        row.earned = w.ai_robots;
    } else {
        int found = 0;
        for (const auto& agent : expected) {
            std::string a = agent;
            std::transform(a.begin(), a.end(), a.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (present.count(a)) ++found;
            else add_issue(row, r, GeoSeverity::Warning, "robots.txt",
                           "AI crawler '" + agent + "' block missing from robots.txt");
        }
        row.earned = frac_score(found, static_cast<int>(expected.size()), w.ai_robots);
    }
    r.checks.push_back(std::move(row));
}

// Check 3: JSON-LD parse validity + per-schema required-field validation
void check_json_ld(const std::vector<HtmlPage>& pages, const Config& cfg,
                   GeoAuditResult& r, const CheckWeights& w) {
    GeoCheckScore row{GeoCheck::JsonLd, check_label(GeoCheck::JsonLd), 0, w.json_ld, {}};
    if (!cfg.json_ld_enabled) {
        row.max = 0; // skipped
        r.checks.push_back(std::move(row));
        return;
    }
    int valid_pages = 0;
    int total_pages = static_cast<int>(pages.size());
    for (const auto& page : pages) {
        if (page.had_parse_error) {
            add_issue(row, r, GeoSeverity::Error, page.rel_path,
                      "unparseable JSON-LD (" + page.parse_error_snippet + ")");
            // a parse-error page is NOT a valid page even if other blocks parse
            continue;
        }
        bool page_has_issue = false;
        for (const auto& s : page.schemas) {
            if (!s.is_object()) continue;
            if (schema_type(s).empty()) continue;
            // validate() returns issues only for types it knows about
            // (WebPage / BlogPosting / Article / ...). Site-wide scaffolding
            // schemas (WebSite, Organization, BreadcrumbList) are no-ops here.
            for (const auto& si : seo::validate(s, page.rel_path)) {
                add_issue(row, r, GeoSeverity::Warning, page.rel_path,
                          std::string("missing or empty required field '")
                          + si.field + "' (on @" + si.schema_type + ")");
                page_has_issue = true;
            }
        }
        // A page with a validation warning isn't "fully valid" — it still
        // counts in the denominator but not the numerator.
        if (!page_has_issue) ++valid_pages;
    }
    row.earned = frac_score(valid_pages, total_pages, w.json_ld);
    r.checks.push_back(std::move(row));
}

// Check 4: Organization consistency
void check_org(const std::vector<HtmlPage>& pages, const Config& cfg,
               GeoAuditResult& r, const CheckWeights& w) {
    GeoCheckScore row{GeoCheck::OrgConsistency, check_label(GeoCheck::OrgConsistency), 0, w.org, {}};
    if (cfg.org_name.empty()) {
        row.max = 0; // skipped
        r.checks.push_back(std::move(row));
        return;
    }

    // 4a. Site-wide validate_organization warnings (logo file, same_as URLs...).
    for (const auto& oi : seo::validate_organization(cfg, {})) {
        add_issue(row, r, GeoSeverity::Warning, "",
                  std::string("org '") + oi.field + "': " + oi.message);
    }

    // 4b. Cross-page Organization schema divergence: collect the distinct
    // values seen for name/logo/url across every Organization JSON-LD block.
    std::set<std::string> names, logos, urls;
    for (const auto& page : pages) {
        for (const auto& s : page.schemas) {
            if (schema_type(s) != "Organization") continue;
            if (s.contains("name") && s["name"].is_string())
                names.insert(s["name"].get<std::string>());
            if (s.contains("logo") && s["logo"].is_string())
                logos.insert(s["logo"].get<std::string>());
            else if (s.contains("logo") && s["logo"].is_object()
                     && s["logo"].contains("url") && s["logo"]["url"].is_string())
                logos.insert(s["logo"]["url"].get<std::string>());
            if (s.contains("url") && s["url"].is_string())
                urls.insert(s["url"].get<std::string>());
        }
    }
    bool divergent = false;
    if (names.size() > 1) {
        divergent = true;
        std::string joined;
        for (const auto& v : names) { if (!joined.empty()) joined += ", "; joined += "'" + v + "'"; }
        add_issue(row, r, GeoSeverity::Warning, "",
                  "Organization 'name' diverges across pages: " + joined);
    }
    if (logos.size() > 1) {
        divergent = true;
        std::string joined;
        for (const auto& v : logos) { if (!joined.empty()) joined += ", "; joined += "'" + v + "'"; }
        add_issue(row, r, GeoSeverity::Warning, "",
                  "Organization 'logo' diverges across pages: " + joined);
    }
    if (urls.size() > 1) {
        divergent = true;
        std::string joined;
        for (const auto& v : urls) { if (!joined.empty()) joined += ", "; joined += "'" + v + "'"; }
        add_issue(row, r, GeoSeverity::Warning, "",
                  "Organization 'url' diverges across pages: " + joined);
    }

    row.earned = divergent ? 0 : w.org;
    r.checks.push_back(std::move(row));
}

// Check 5: Author profile pages exist for each loaded author
void check_author_pages(const fs::path& out, const Config& cfg,
                        GeoAuditResult& r, const CheckWeights& w) {
    GeoCheckScore row{GeoCheck::AuthorPages, check_label(GeoCheck::AuthorPages), 0, w.authors, {}};
    if (!cfg.authors_enabled) {
        row.max = 0; // skipped
        r.checks.push_back(std::move(row));
        return;
    }
    // URL base mirrors builder.cpp's G6 Phase 1.8 convention:
    //   "/" + basename(authors_dir) + "/"
    std::string base_name = fs::path(cfg.authors_dir).filename().string();
    if (base_name.empty()) base_name = "authors";
    std::string url_base = "/" + base_name + "/";

    // Enumerate <slug>.md files under authors_dir.
    std::vector<std::string> slugs;
    std::error_code ec;
    fs::path authors_path = fs::weakly_canonical(fs::path(cfg.authors_dir), ec);
    if (fs::is_directory(authors_path, ec)) {
        for (auto& entry : fs::directory_iterator(authors_path, ec)) {
            if (ec) { ec.clear(); continue; }
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".md") continue;
            slugs.push_back(entry.path().stem().string());
        }
    }

    if (slugs.empty()) {
        // Nothing to check — vacuous full credit.
        row.earned = w.authors;
        r.checks.push_back(std::move(row));
        return;
    }

    int present = 0;
    for (const auto& slug : slugs) {
        fs::path expected = out / base_name / slug / "index.html";
        if (fs::exists(expected)) {
            ++present;
        } else {
            add_issue(row, r, GeoSeverity::Warning, "",
                      "missing profile page for '" + slug + "' (expected " +
                      url_base + slug + "/)");
        }
    }
    row.earned = frac_score(present, static_cast<int>(slugs.size()), w.authors);
    r.checks.push_back(std::move(row));
}

// Check 6: Citation tags on article pages
void check_citation_tags(const std::vector<HtmlPage>& pages, const Config& cfg,
                         GeoAuditResult& r, const CheckWeights& w) {
    GeoCheckScore row{GeoCheck::CitationTags, check_label(GeoCheck::CitationTags), 0, w.citation_tags, {}};
    if (!cfg.citation_tags_enabled) {
        row.max = 0; // skipped
        r.checks.push_back(std::move(row));
        return;
    }
    int articles = 0;
    int tagged   = 0;
    for (const auto& page : pages) {
        bool is_article = false;
        for (const auto& s : page.schemas) {
            if (is_article_type(schema_type(s))) { is_article = true; break; }
        }
        if (!is_article) continue;
        ++articles;
        bool has_author = page.citation_meta_names.count("author") > 0;
        bool has_title  = page.citation_meta_names.count("title")  > 0;
        if (has_author && has_title) {
            ++tagged;
        } else {
            std::string missing;
            if (!has_title)  missing += "citation_title";
            if (!has_author) {
                if (!missing.empty()) missing += ", ";
                missing += "citation_author";
            }
            add_issue(row, r, GeoSeverity::Warning, page.rel_path,
                      "article page missing " + missing + " meta tag(s)");
        }
    }
    row.earned = frac_score(tagged, articles, w.citation_tags);
    r.checks.push_back(std::move(row));
}

// Check 7: Passage index (hasPart) on prose pages
void check_passage_index(const std::vector<HtmlPage>& pages, const Config& cfg,
                         GeoAuditResult& r, const CheckWeights& w) {
    GeoCheckScore row{GeoCheck::PassageIndex, check_label(GeoCheck::PassageIndex), 0, w.passage_index, {}};
    if (!cfg.json_ld_enabled) {
        row.max = 0; // skipped (same gate as Check 3 — hasPart emits with JSON-LD)
        r.checks.push_back(std::move(row));
        return;
    }
    int prose = 0;
    int with_passages = 0;
    for (const auto& page : pages) {
        const json* page_schema = find_page_schema(page.schemas);
        if (page_schema == nullptr) continue;
        if (!is_article_type(schema_type(*page_schema))) continue;
        ++prose;
        if (has_nonempty_haspart(*page_schema)) {
            ++with_passages;
        } else {
            add_issue(row, r, GeoSeverity::Warning, page.rel_path,
                      "no hasPart in page schema (passage index missing)");
        }
    }
    row.earned = frac_score(with_passages, prose, w.passage_index);
    r.checks.push_back(std::move(row));
}

// Check 8: AI sitemap
void check_ai_sitemap(const fs::path& out, const Config& cfg,
                      GeoAuditResult& r, const CheckWeights& w) {
    GeoCheckScore row{GeoCheck::AiSitemap, check_label(GeoCheck::AiSitemap), 0, w.ai_sitemap, {}};
    if (!cfg.module_sitemap_ai) {
        row.max = 0; // skipped
        r.checks.push_back(std::move(row));
        return;
    }
    fs::path p = out / "sitemap-ai.xml";
    if (!fs::exists(p)) {
        add_issue(row, r, GeoSeverity::Error, "sitemap-ai.xml",
                  "sitemap-ai.xml missing (module_sitemap_ai=true)");
    } else {
        std::string txt = utils::read_file_or_empty(p.string());
        if (txt.find("<urlset") == std::string::npos) {
            add_issue(row, r, GeoSeverity::Error, "sitemap-ai.xml",
                      "sitemap-ai.xml is not a valid sitemap (no <urlset> root)");
        } else {
            row.earned = w.ai_sitemap;
        }
    }
    r.checks.push_back(std::move(row));
}

// Check 9: FAQ coverage (informational only)
void check_faq_coverage(const std::vector<HtmlPage>& pages, const Config& /*cfg*/,
                        GeoAuditResult& r, const CheckWeights& /*w*/) {
    GeoCheckScore row{GeoCheck::FaqCoverage, check_label(GeoCheck::FaqCoverage), 0, 0, {}};
    int faq_pages = 0;
    for (const auto& page : pages) {
        for (const auto& s : page.schemas) {
            if (schema_type(s) == "FAQPage") { ++faq_pages; break; }
        }
    }
    // Single Info line reports the count; no score impact.
    row.issues.push_back({GeoSeverity::Info, GeoCheck::FaqCoverage, "",
                          std::to_string(faq_pages) + " page(s) with FAQPage"});
    r.checks.push_back(std::move(row));
}

// Scorecard symbol helpers (colored when stdout is a TTY).
std::string ok_sym()    { return utils::colorize(utils::color::green,  "+"); }
std::string fail_sym()  { return utils::colorize(utils::color::red,    "X"); }
std::string warn_sym()  { return utils::colorize(utils::color::yellow, "!"); }
std::string info_sym()  { return utils::colorize(utils::color::cyan,   "i"); }

// Pad/trim a label to a fixed column width.
std::string pad_label(const std::string& s, size_t width) {
    if (s.size() >= width) return s.substr(0, width);
    return s + std::string(width - s.size(), ' ');
}

} // anonymous namespace

GeoAuditResult audit_geo(const std::string& output_dir, const Config& cfg) {
    GeoAuditResult r;
    CheckWeights w;

    std::error_code ec;
    fs::path output_root = fs::weakly_canonical(fs::path(output_dir), ec);
    if (ec || !fs::is_directory(output_root, ec)) {
        // Mirror link_checker's behavior: single hard Error, score 0.
        GeoCheckScore row{GeoCheck::LlmsTxt, "output directory", 0, 0, {}};
        row.issues.push_back({GeoSeverity::Error, GeoCheck::LlmsTxt, output_dir,
                              "output directory '" + output_dir +
                              "' does not exist — run `cstatic build` first"});
        r.checks.push_back(std::move(row));
        r.score = 0;
        r.hard_count = 1;
        return r;
    }

    // One shared walk over output/ — parsed JSON-LD reused across checks.
    std::vector<HtmlPage> pages = collect_html_pages(output_root);

    check_llms_txt(output_root, cfg, r, w);
    check_ai_robots(output_root, cfg, r, w);
    check_json_ld(pages, cfg, r, w);
    check_org(pages, cfg, r, w);
    check_author_pages(output_root, cfg, r, w);
    check_citation_tags(pages, cfg, r, w);
    check_passage_index(pages, cfg, r, w);
    check_ai_sitemap(output_root, cfg, r, w);
    check_faq_coverage(pages, cfg, r, w);

    // Score = round(sum(earned) / sum(max_of_non_skipped) * 100). Skipped
    // checks (max==0) contribute to neither side. All 9 rows are ALWAYS kept
    // so the scorecard shows every check the audit knows about — skipped rows
    // render as `[--]`, which makes "nothing enabled" self-evident without a
    // separate banner row.
    int sum_earned = 0;
    int sum_max    = 0;
    for (const auto& c : r.checks) {
        if (c.max > 0) {
            sum_earned += c.earned;
            sum_max    += c.max;
        }
    }
    if (sum_max > 0) {
        double pct = static_cast<double>(sum_earned)
                   / static_cast<double>(sum_max) * 100.0;
        r.score = static_cast<int>(std::lround(pct));
        if (r.score < 0)   r.score = 0;
        if (r.score > 100) r.score = 100;
    } else {
        r.score = 0; // nothing enabled
    }
    return r;
}

std::string format_geo_report(const GeoAuditResult& r) {
    std::ostringstream out;
    out << "\n";

    for (const auto& c : r.checks) {
        std::string symbol;
        std::string score_text;
        std::string label_text = c.label;
        bool render_issues_inline = false; // FAQ embeds its count in the label

        // Detect issue mix on this row.
        bool has_error = false;
        bool has_info  = false;
        for (const auto& i : c.issues) {
            if (i.severity == GeoSeverity::Error) has_error = true;
            if (i.severity == GeoSeverity::Info)  has_info  = true;
        }

        if (c.check == GeoCheck::FaqCoverage) {
            // FAQ coverage: informational only, count goes inline in the label.
            symbol = info_sym();
            std::string msg = c.issues.empty() ? std::string("0 page(s)")
                                               : c.issues.front().message;
            label_text = std::string("FAQ coverage: ") + msg;
            score_text = "[info]";
            render_issues_inline = true; // don't double-print the Info line
        } else if (has_error) {
            // Any Error on the row makes it a failure regardless of partial
            // credit — matches the scorecard's ✗ in the plan example.
            symbol = fail_sym();
            score_text = "[" + std::to_string(c.earned) + "/" + std::to_string(c.max) + "]";
        } else if (c.max == 0) {
            // Skipped (Info-only, no weight) — e.g. feature disabled, or the
            // "no GEO features enabled" recommendation row.
            symbol = info_sym();
            score_text = "[--]";
        } else if (c.earned >= c.max) {
            symbol = ok_sym();
            score_text = "[" + std::to_string(c.earned) + "/" + std::to_string(c.max) + "]";
        } else {
            // Partial credit on warnings only.
            symbol = warn_sym();
            score_text = "[" + std::to_string(c.earned) + "/" + std::to_string(c.max) + "]";
        }
        (void)has_info;

        out << symbol << "  "
            << pad_label(label_text, 45)
            << " " << score_text << "\n";

        // Detail lines: Errors and Warnings always render; Info renders too
        // (for the "no GEO features enabled" row) unless this row already
        // inlined its Info (FAQ coverage).
        if (!render_issues_inline) {
            for (const auto& i : c.issues) {
                std::string label;
                if (i.severity == GeoSeverity::Error) {
                    label = utils::error_label();
                } else if (i.severity == GeoSeverity::Warning) {
                    label = utils::warning_label();
                } else {
                    label = utils::info_label();
                }
                out << "    " << label << " ";
                if (!i.file.empty()) out << i.file << ": ";
                out << i.message << "\n";
            }
        }
    }

    out << "\n" << utils::colorize(utils::color::bold,
                                   "GEO Score: " + std::to_string(r.score) + "/100") << "\n";

    // Detect "nothing enabled": every non-FAQ check is skipped (max==0). In
    // that case the score is 0 by definition and a hint to enable features is
    // more useful than a bare "No issues" line.
    bool any_enabled = false;
    for (const auto& c : r.checks) {
        if (c.check != GeoCheck::FaqCoverage && c.max > 0) { any_enabled = true; break; }
    }

    if (!any_enabled && r.hard_count == 0 && r.soft_count == 0) {
        out << utils::info_label() << " No GEO features are enabled — set "
            << "modules.llms_txt, seo.json_ld_enabled, "
            << "modules.robots_ai_crawlers_mode, etc. in config.toml and rebuild.\n";
    } else if (r.hard_count > 0 || r.soft_count > 0) {
        // Lead with error: only when there are hard issues; otherwise the
        // warnings-only case reads better starting with warn:.
        if (r.hard_count > 0) {
            out << utils::error_label() << " "
                << r.hard_count << " hard issue" << (r.hard_count == 1 ? "" : "s");
            if (r.soft_count > 0) {
                out << ", " << utils::warning_label() << " "
                    << r.soft_count << " warning" << (r.soft_count == 1 ? "" : "s");
            }
        } else {
            out << utils::warning_label() << " "
                << r.soft_count << " warning" << (r.soft_count == 1 ? "" : "s");
        }
        out << "\n";
    } else {
        out << utils::success_label() << " No GEO issues found.\n";
    }
    return out.str();
}

} // namespace pipeline
} // namespace cstatic
