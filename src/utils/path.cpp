#include "utils/path.hpp"

#include <filesystem>

namespace cstatic::utils {

namespace fs = std::filesystem;

std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    // Strip leading slash from b to prevent fs::path treating it as absolute
    std::string b_clean = b;
    while (!b_clean.empty() && b_clean.front() == '/') {
        b_clean = b_clean.substr(1);
    }
    if (b_clean.empty()) return a;
    return (fs::path(a) / b_clean).string();
}

bool ensure_dir(const std::string& path) {
    return fs::create_directories(path);
}

std::string source_to_url(const std::string& file_path, const std::string& source_dir) {
    // Get relative path from source_dir using filesystem
    fs::path rel = fs::relative(fs::path(file_path), fs::path(source_dir));
    std::string rel_str = rel.generic_string();

    // Strip .md extension
    if (rel_str.size() > 3 && rel_str.substr(rel_str.size() - 3) == ".md") {
        rel_str = rel_str.substr(0, rel_str.size() - 3);
    }

    // index.md → /
    if (rel_str == "index") {
        return "/";
    }

    // foo/index.md → /foo/
    if (rel_str.size() > 6 && rel_str.substr(rel_str.size() - 6) == "/index") {
        return "/" + rel_str.substr(0, rel_str.size() - 5);
    }

    // foo/bar.md → /foo/bar/
    return "/" + rel_str + "/";
}

std::string url_to_output(const std::string& url, const std::string& output_dir) {
    std::string rel = url;

    // Strip leading slash
    while (!rel.empty() && rel.front() == '/') {
        rel = rel.substr(1);
    }

    // Root URL → index.html
    if (rel.empty()) {
        return (fs::path(output_dir) / "index.html").string();
    }

    // Ensure trailing slash → index.html
    if (rel.back() == '/') {
        return (fs::path(output_dir) / (rel + "index.html")).string();
    }

    return (fs::path(output_dir) / (rel + "/index.html")).string();
}

std::string parent_dir(const std::string& path) {
    std::string parent = fs::path(path).parent_path().string();
    return parent.empty() ? "." : parent;
}

std::string replace_extension(const std::string& path, const std::string& new_ext) {
    fs::path p(path);
    p.replace_extension(new_ext);
    return p.string();
}

} // namespace cstatic::utils
