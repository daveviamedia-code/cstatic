#pragma once

#include <string>
#include <vector>

namespace cstatic {

struct Config;

namespace pipeline {

// Severity of a single GEO audit finding.
enum class GeoSeverity {
    Info,
    Warning,
    Error
};

// Which of the 9 checks produced this finding. Order matches the scorecard.
enum class GeoCheck {
    LlmsTxt,        // 1 — modules.llms_txt
    AiRobots,       // 2 — modules.robots_ai_crawlers_mode
    JsonLd,         // 3 — seo.json_ld_enabled
    OrgConsistency, // 4 — seo.org_name
    AuthorPages,    // 5 — authors.enabled
    CitationTags,   // 6 — seo.citation_tags_enabled
    PassageIndex,   // 7 — seo.json_ld_enabled (hasPart on prose pages)
    AiSitemap,      // 8 — modules.sitemap_ai
    FaqCoverage     // 9 — informational only (0 weight)
};

// A single audit finding.
struct GeoIssue {
    GeoSeverity severity;
    GeoCheck    check;
    std::string file;       // output-dir-relative path or empty
    std::string message;
};

// Per-check scorecard row.
struct GeoCheckScore {
    GeoCheck    check;
    std::string label;       // e.g. "llms.txt"
    int         earned = 0;
    int         max    = 0;  // 0 ⇒ skipped (Info only, doesn't contribute)
    std::vector<GeoIssue> issues;
};

// Full audit result.
struct GeoAuditResult {
    int score = 0;                          // 0..100 (rounded)
    std::vector<GeoCheckScore> checks;      // one per check, in fixed order
    int hard_count = 0;                     // Error-severity issue count
    int soft_count = 0;                     // Warning-severity issue count
};

// Scan output_dir and score GEO readiness. Reads only files in output_dir
// plus cfg.authors_dir (to enumerate expected author profile pages). Pure
// function — no stdout/stderr output. Returns early with a single hard Error
// when output_dir does not exist (mirrors link_checker's behavior).
GeoAuditResult audit_geo(const std::string& output_dir, const Config& cfg);

// Format the result as the human-readable report (string with newlines).
// Uses utils::colorize + success_label/error_label/warning_label/info_label
// for consistency with cmd_check output.
std::string format_geo_report(const GeoAuditResult& r);

} // namespace pipeline
} // namespace cstatic
