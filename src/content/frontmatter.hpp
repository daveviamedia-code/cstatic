#pragma once

#include <string>
#include <ctime>
#include <nlohmann/json.hpp>

namespace cstatic {

// Metadata extracted from YAML frontmatter.
struct Frontmatter {
    std::string title;
    std::string layout = "default";
    std::string permalink;        // empty = auto-compute from file path
    std::string date;             // ISO date string, e.g. "2024-01-15"
    std::vector<std::string> tags;
    std::vector<std::string> aliases;
    bool draft = false;
    std::string description;          // SEO description (falls back to excerpt)
    std::string image;                // OG image URL (relative or absolute)
    std::string canonical;            // canonical URL override
    std::string sitemap_changefreq;   // e.g. "monthly"
    std::string sitemap_priority;     // e.g. "0.8" (string for tolerant YAML parsing)
    nlohmann::json custom = nlohmann::json::object();  // any extra key-value pairs
};

// Result of parsing a content file: frontmatter + body (without the --- delimiters).
struct ParsedContent {
    Frontmatter frontmatter;
    std::string body;   // raw markdown body
};

// Parse a file that may contain YAML frontmatter.
// Frontmatter is optional — a `---` block at the very start of the file.
// If no frontmatter is present, the entire file content is returned as body.
ParsedContent parse_frontmatter(const std::string& content, const std::string& filename);

} // namespace cstatic
