#pragma once

#include <string>

namespace cstatic::utils {

// Join two path segments with exactly one '/' separator.
std::string path_join(const std::string& a, const std::string& b);

// Ensure a directory exists (creates parents as needed). Returns true on success.
bool ensure_dir(const std::string& path);

// Compute the URL path for a source file relative to source_dir.
// e.g. ("src/posts/hello.md", "src") → "/posts/hello/"
std::string source_to_url(const std::string& file_path, const std::string& source_dir);

// Compute the output file path for a given URL.
// e.g. ("/posts/hello/", "output") → "output/posts/hello/index.html"
std::string url_to_output(const std::string& url, const std::string& output_dir);

// Get the parent directory of a path.
std::string parent_dir(const std::string& path);

// Replace extension (or add one if none exists).
std::string replace_extension(const std::string& path, const std::string& new_ext);

} // namespace cstatic::utils
