#pragma once

#include <string>
#include <map>
#include <ctime>

namespace cstatic {

// Metadata extracted from YAML frontmatter.
struct Frontmatter {
    std::string title;
    std::string layout = "default";
    std::string permalink;        // empty = auto-compute from file path
    std::string date;             // ISO date string, e.g. "2024-01-15"
    std::vector<std::string> tags;
    bool draft = false;
    std::map<std::string, std::string> custom;  // any extra key-value pairs
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
