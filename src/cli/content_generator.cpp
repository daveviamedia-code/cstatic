#include "cli/content_generator.hpp"

#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>

#include "utils/terminal.hpp"

namespace fs = std::filesystem;
using namespace cstatic::utils;

namespace cstatic::cli {

namespace {

// Current local date as YYYY-MM-DD.
std::string current_date() {
    auto now = std::time(nullptr);
    std::tm tm = *std::localtime(&now);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d");
    return ss.str();
}

// Read an entire file into `out`. Returns false on open failure.
bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Replace every "{{ key }}" (tolerant of inner whitespace) with value.
std::string substitute(const std::string& body, const std::string& key, const std::string& value) {
    std::regex re("\\{\\{\\s*" + key + "\\s*\\}\\}");
    return std::regex_replace(body, re, value);
}

// Derive a human-readable title from a filename stem.
// "my-cool_post" -> "My Cool Post"
std::string derive_title(const std::string& stem) {
    std::string title;
    bool capitalize_next = true;
    for (char c : stem) {
        if (c == '-' || c == '_' || c == '.') {
            title.push_back(' ');
            capitalize_next = true;
        } else if (capitalize_next) {
            title.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            capitalize_next = false;
        } else {
            title.push_back(c);
        }
    }
    return title;
}

// Built-in default archetype, used when no archetype file is found.
const char* builtin_default_archetype() {
    return "---\n"
           "title: \"{{ title }}\"\n"
           "date: \"{{ date }}\"\n"
           "---\n\n"
           "# {{ title }}\n";
}

} // anonymous namespace

int generate_content(const std::string& target_file,
                     const std::string& archetypes_dir,
                     const std::string& kind,
                     const std::string& today) {
    const std::string date = today.empty() ? current_date() : today;

    fs::path target(target_file);
    std::string slug = target.stem().string();
    if (slug.empty() || slug == "." || slug == "..") {
        std::cerr << error_label() << " cannot derive a filename from '" << target_file << "'\n";
        return 1;
    }
    std::string title = derive_title(slug);

    // Resolve the archetype: <kind>.md, then default.md, then built-in.
    std::string chosen_kind = kind.empty() ? "default" : kind;
    std::string primary = (fs::path(archetypes_dir) / (chosen_kind + ".md")).string();
    std::string body;
    bool loaded = read_file(primary, body);

    if (!loaded && chosen_kind != "default") {
        std::cerr << warning_label() << " archetype '" << kind << "' not found at "
                  << primary << "; using default\n";
        std::string fallback = (fs::path(archetypes_dir) / "default.md").string();
        loaded = read_file(fallback, body);
    }
    if (!loaded) {
        body = builtin_default_archetype();
    }

    // Substitute placeholders.
    body = substitute(body, "title", title);
    body = substitute(body, "slug", slug);
    body = substitute(body, "date", date);

    // Refuse to overwrite an existing file.
    if (fs::exists(target_file)) {
        std::cerr << error_label() << " file already exists: " << target_file << "\n"
                  << "  Remove it first or choose a different path.\n";
        return 1;
    }

    // Create parent directories as needed.
    std::error_code ec;
    fs::path parent = target.parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
        if (!fs::create_directories(parent, ec)) {
            std::cerr << error_label() << " cannot create directory " << parent.string()
                      << ": " << ec.message() << "\n";
            return 1;
        }
    }

    // Write the file.
    std::ofstream out(target_file, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << error_label() << " cannot create file: " << target_file << "\n";
        return 1;
    }
    out << body;
    out.flush();
    if (!out.good()) {
        std::cerr << error_label() << " failed writing to: " << target_file << "\n";
        return 1;
    }

    std::cout << "  " << colorize(color::green, "created") << "  " << target_file << "\n";
    return 0;
}

} // namespace cstatic::cli
