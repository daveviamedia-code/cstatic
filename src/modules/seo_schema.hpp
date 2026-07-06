#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace cstatic {

struct Config;

namespace modules {
namespace seo_schema {

// A validation issue against a generated page schema.
struct SchemaIssue {
    std::string page_url;
    std::string schema_type;
    std::string field;
    std::string message;
};

// Returns concatenated <script type="application/ld+json"> blocks for one
// page: WebSite + Organization (if configured) + page schema +
// BreadcrumbList (for nested pages) + each schema_extra entry. Empty string
// when cfg.json_ld_enabled is false.
//
//   page   title/url/date/description/image/canonical/excerpt/tags +
//          custom frontmatter (type, author, schema, schema_extra, keywords)
//   pages  full pages_array, used for BreadcrumbList ancestor resolution
//
// Validation problems are written to stderr (non-fatal).
std::string build_json_ld(const Config& cfg,
                          const nlohmann::json& page,
                          const nlohmann::json& pages);

// Standalone site-wide WebSite schema as a single <script> block.
std::string build_website_script(const Config& cfg);

// Standalone site-wide Organization schema as a single <script> block.
// Empty string when cfg.org_name is unset.
std::string build_organization_script(const Config& cfg);

// Validate a finished page schema object against its @type. Returns a list
// of required fields that are missing or empty. Unknown types → empty list.
std::vector<SchemaIssue> validate(const nlohmann::json& page_schema,
                                  const std::string& page_url);

// Build citation_* meta tags (Google Scholar, Perplexity, ChatGPT) for one
// page. Returns a string of <meta name="citation_*" content="..."> tags.
// Empty when cfg.citation_tags_enabled is false.
//
//   page   title/url/date/description/tags +
//          custom frontmatter (author, pdf_url, journal, doi, tldr)
std::string build_citation_tags(const Config& cfg, const nlohmann::json& page);

} // namespace seo_schema
} // namespace modules
} // namespace cstatic
