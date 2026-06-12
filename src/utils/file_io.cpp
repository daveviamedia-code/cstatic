#include "utils/file_io.hpp"
#include "utils/terminal.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace cstatic::utils {

namespace fs = std::filesystem;

// Ensure the parent directory of `path` exists. Creates directories as needed.
static void ensure_parent_dir(const std::string& path) {
    auto dir = fs::path(path).parent_path();
    if (!dir.empty() && !fs::exists(dir)) {
        fs::create_directories(dir);
    }
}

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error(
            std::string(error_label()) + " cannot read file: " + path +
            " — file does not exist or is unreadable");
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string read_file_or_empty(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string read_file_binary(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        throw std::runtime_error(
            std::string(error_label()) + " cannot read file: " + path +
            " — file does not exist or is unreadable");
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void write_file(const std::string& path, const std::string& content) {
    ensure_parent_dir(path);
    std::ofstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error(
            std::string(error_label()) + " cannot write file: " + path +
            " — check that the output directory is writable");
    }
    f << content;
}

void write_file_binary(const std::string& path, const std::string& content) {
    ensure_parent_dir(path);
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) {
        throw std::runtime_error(
            std::string(error_label()) + " cannot write file: " + path +
            " — check that the output directory is writable");
    }
    f << content;
}

std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

std::string strip_html_tags(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool in_tag = false;
    for (char c : html) {
        if (c == '<') { in_tag = true; continue; }
        if (c == '>') { in_tag = false; continue; }
        if (!in_tag) out += c;
    }
    return out;
}

std::string truncate_text(const std::string& text, size_t max_len) {
    if (text.size() <= max_len) return text;
    // Find last space before max_len
    size_t pos = text.rfind(' ', max_len);
    if (pos == std::string::npos || pos == 0) pos = max_len;
    return text.substr(0, pos) + "...";
}

} // namespace cstatic::utils
