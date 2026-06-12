#pragma once

#include <string>

namespace cstatic::utils {

// Read file contents as text. Throws std::runtime_error on failure.
std::string read_file(const std::string& path);

// Read file contents as text. Returns empty string on failure (no throw).
std::string read_file_or_empty(const std::string& path);

// Read file contents as binary. Throws std::runtime_error on failure.
std::string read_file_binary(const std::string& path);

// Write text content to file, creating parent directories as needed.
// Throws std::runtime_error on failure.
void write_file(const std::string& path, const std::string& content);

// Write binary content to file, creating parent directories as needed.
// Throws std::runtime_error on failure.
void write_file_binary(const std::string& path, const std::string& content);

// Escape special characters for XML/HTML (&, <, >, ", ').
std::string xml_escape(const std::string& s);

// Strip all HTML tags from a string, returning plain text.
std::string strip_html_tags(const std::string& html);

// Truncate text to max_len characters, breaking at the last word boundary.
std::string truncate_text(const std::string& text, size_t max_len);

} // namespace cstatic::utils
