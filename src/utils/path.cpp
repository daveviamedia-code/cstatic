#include "utils/path.hpp"

#include <cstdlib>
#include <sys/stat.h>
#include <filesystem>

namespace cstatic::utils {

std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/' && b.front() == '/') {
        return a + b.substr(1);
    }
    if (a.back() != '/' && b.front() != '/') {
        return a + "/" + b;
    }
    return a + b;
}

bool ensure_dir(const std::string& path) {
    return std::filesystem::create_directories(path);
}

std::string source_to_url(const std::string& file_path, const std::string& source_dir) {
    // Get relative path from source_dir
    std::string rel;
    if (file_path.size() > source_dir.size() &&
        file_path.substr(0, source_dir.size()) == source_dir) {
        rel = file_path.substr(source_dir.size());
    } else {
        rel = file_path;
    }

    // Strip leading slash
    while (!rel.empty() && rel.front() == '/') {
        rel = rel.substr(1);
    }

    // Strip .md extension
    if (rel.size() > 3 && rel.substr(rel.size() - 3) == ".md") {
        rel = rel.substr(0, rel.size() - 3);
    }

    // index.md → /
    if (rel == "index") {
        return "/";
    }

    // foo/index.md → /foo/
    if (rel.size() > 6 && rel.substr(rel.size() - 6) == "/index") {
        return "/" + rel.substr(0, rel.size() - 5);
    }

    // foo/bar.md → /foo/bar/
    return "/" + rel + "/";
}

std::string url_to_output(const std::string& url, const std::string& output_dir) {
    std::string rel = url;

    // Strip leading slash
    while (!rel.empty() && rel.front() == '/') {
        rel = rel.substr(1);
    }

    // Root URL → index.html
    if (rel.empty()) {
        return path_join(output_dir, "index.html");
    }

    // Ensure trailing slash → index.html
    if (rel.back() == '/') {
        return path_join(output_dir, rel + "index.html");
    }

    return path_join(output_dir, rel + "/index.html");
}

std::string parent_dir(const std::string& path) {
    auto pos = path.rfind('/');
    if (pos == std::string::npos) return ".";
    return path.substr(0, pos);
}

std::string replace_extension(const std::string& path, const std::string& new_ext) {
    auto pos = path.rfind('.');
    auto slash = path.rfind('/');
    if (pos == std::string::npos || (slash != std::string::npos && pos < slash)) {
        return path + new_ext;
    }
    return path.substr(0, pos) + new_ext;
}

} // namespace cstatic::utils
