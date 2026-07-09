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

// G10: A validation issue against the site-wide Organization identity.
struct OrgIssue {
    std::string field;   // e.g. "org_name", "org_logo", "org_same_as[0]", "org_founders"
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

// G10: Validate the site-wide Organization identity for consistency. Returns
// non-fatal issues. Checks: org_name vs site_title divergence, org_logo file
// existence (when not an absolute URL), same_as URL format, and org_founders
// references against known author slugs. known_author_slugs may be empty (the
// founders check is skipped). Returns an empty list when org_name is unset.
std::vector<OrgIssue> validate_organization(const Config& cfg,
                                             const std::vector<std::string>& known_author_slugs);

// G10: Template-friendly Organization context for {{ org }}. Returns an empty
// object when org_name is unset. Derived from the same cfg fields as the
// JSON-LD schema so there is a single source of truth. Keys: name, url,
// legal_name, logo_url, founding_date, founders[], same_as[] (only the
// non-empty ones are included).
nlohmann::json build_org_context(const Config& cfg);

} // namespace seo_schema
} // namespace modules
} // namespace cstatic
