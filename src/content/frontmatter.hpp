#pragma once

#include <string>
#include <ctime>
#include <stdexcept>
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

// Thrown when frontmatter parsing fails. Carries line+column for structured
// error reporting (mirrors the RenderError pattern in template/renderer.hpp).
class FrontmatterError : public std::runtime_error {
public:
    FrontmatterError(const std::string& source_file, int line, int column,
                     const std::string& message)
        : std::runtime_error(message),
          source_file_(source_file), line_(line), column_(column) {}
    const std::string& source_file() const { return source_file_; }
    int line() const { return line_; }
    int column() const { return column_; }
private:
    std::string source_file_;
    int line_;
    int column_;
};

// Parse a file that may contain YAML frontmatter.
// Frontmatter is optional — a `---` block at the very start of the file.
// If no frontmatter is present, the entire file content is returned as body.
ParsedContent parse_frontmatter(const std::string& content, const std::string& filename);

} // namespace cstatic
