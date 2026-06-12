#include "assets/asset_pipeline.hpp"
#include "hash/hash_store.hpp"
#include "utils/path.hpp"
#include "utils/terminal.hpp"
#include "utils/file_io.hpp"

#include <filesystem>
#include <iostream>
#include <algorithm>
#include <unordered_set>

namespace cstatic {

namespace fs = std::filesystem;

// Copy file preserving permissions.
static void copy_file(const std::string& src, const std::string& dst) {
    std::string dir = utils::parent_dir(dst);
    if (!dir.empty() && !fs::exists(dir)) {
        fs::create_directories(dir);
    }
    std::error_code ec;
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        throw std::runtime_error(
            std::string(utils::error_label()) + " cannot copy asset: " + src +
            " -> " + dst + ": " + ec.message());
    }
}

// Check if a filename should be skipped (hidden files, OS junk).
static bool should_skip(const std::string& filename) {
    if (filename.empty()) return true;
    if (filename.front() == '.') return true;  // hidden files
    if (filename == "Thumbs.db") return true;
    return false;
}

// Get file extension (lowercase, includes dot).
static std::string get_extension(const std::string& path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    std::string ext = path.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext;
}

// --- CSS Minification ---

std::string minify_css(const std::string& input) {
    if (input.empty()) return input;

    std::string out;
    out.reserve(input.size());

    size_t i = 0;
    size_t len = input.size();

    while (i < len) {
        // Block comment: /* ... */
        if (i + 1 < len && input[i] == '/' && input[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(input[i] == '*' && input[i + 1] == '/')) {
                i++;
            }
            i += 2; // skip */
            continue;
        }

        // Whitespace handling
        if (input[i] == ' ' || input[i] == '\t' || input[i] == '\n' || input[i] == '\r') {
            // Collapse whitespace to a single space, but only if meaningful
            // Skip all consecutive whitespace
            while (i < len && (input[i] == ' ' || input[i] == '\t' ||
                               input[i] == '\n' || input[i] == '\r')) {
                i++;
            }
            // Only emit space if it's between two non-special characters
            if (!out.empty() && i < len) {
                char last = out.back();
                char next = input[i];
                // Keep space between identifiers/keywords
                bool need_space = (std::isalnum(static_cast<unsigned char>(last)) ||
                                   last == ')' || last == '%') &&
                                  (std::isalnum(static_cast<unsigned char>(next)) ||
                                   next == '.' || next == '#' || next == '[' ||
                                   next == '-' || next == '_');
                if (need_space) {
                    out += ' ';
                }
            }
            continue;
        }

        out += input[i];
        i++;
    }

    // Remove semicolons immediately before closing braces
    std::string cleaned;
    cleaned.reserve(out.size());
    for (size_t j = 0; j < out.size(); j++) {
        if (out[j] == ';' && j + 1 < out.size() && out[j + 1] == '}') {
            continue; // skip semicolon before }
        }
        cleaned += out[j];
    }

    // Remove empty rules: { }
    std::string final_out;
    final_out.reserve(cleaned.size());
    for (size_t j = 0; j < cleaned.size(); j++) {
        if (cleaned[j] == '{' && j + 1 < cleaned.size() && cleaned[j + 1] == '}') {
            j++; // skip the whole { }
            // Remove the selector before it — walk back to strip the selector
            while (!final_out.empty() && final_out.back() != '}' &&
                   final_out.back() != ';' && final_out.back() != '\n') {
                final_out.pop_back();
            }
            continue;
        }
        final_out += cleaned[j];
    }

    return final_out;
}

// --- JS Minification ---

// Check if '/' at position i starts a regex literal by examining preceding output.
static bool starts_regex(const std::string& out) {
    // Walk backwards past whitespace to find the last meaningful token
    int pos = static_cast<int>(out.size()) - 1;
    while (pos >= 0 && (out[pos] == ' ' || out[pos] == '\t' ||
                        out[pos] == '\n' || out[pos] == '\r')) {
        pos--;
    }
    if (pos < 0) return true; // start of input → regex

    char last = out[pos];

    // After these characters, '/' starts a regex
    if (last == '=' || last == '(' || last == '[' || last == ',' ||
        last == ';' || last == '{' || last == '}' || last == '!' ||
        last == '&' || last == '|' || last == '^' || last == '~' ||
        last == '+' || last == '-' || last == '*' || last == '%' ||
        last == '<' || last == '>' || last == '?' || last == ':') {
        return true;
    }

    // After these keywords, '/' starts a regex
    static const char* keywords[] = {
        "return", "typeof", "void", "delete", "throw", "new", "in", "case"
    };
    for (const char* kw : keywords) {
        size_t kw_len = strlen(kw);
        if (pos >= static_cast<int>(kw_len) - 1) {
            size_t start = pos - kw_len + 1;
            bool match = true;
            for (size_t k = 0; k < kw_len; k++) {
                if (out[start + k] != kw[k]) { match = false; break; }
            }
            // Keyword must be preceded by non-identifier char or be at start
            if (match) {
                if (start == 0) return true;
                char before = out[start - 1];
                if (!std::isalnum(static_cast<unsigned char>(before)) &&
                    before != '_' && before != '$') {
                    return true;
                }
            }
        }
    }

    return false; // otherwise, division
}

std::string minify_js(const std::string& input) {
    if (input.empty()) return input;

    enum State { NORMAL, STRING, COMMENT_LINE, COMMENT_BLOCK, REGEX };

    std::string out;
    out.reserve(input.size());

    size_t i = 0;
    size_t len = input.size();
    State state = NORMAL;
    char quote_char = '\0';

    while (i < len) {
        switch (state) {
        case NORMAL:
            // String literals
            if (input[i] == '\'' || input[i] == '"' || input[i] == '`') {
                quote_char = input[i];
                out += input[i];
                i++;
                state = STRING;
                continue;
            }
            // Block comment
            if (i + 1 < len && input[i] == '/' && input[i + 1] == '*') {
                i += 2;
                state = COMMENT_BLOCK;
                continue;
            }
            // Regex vs division vs line comment
            if (input[i] == '/') {
                if (i + 1 < len && input[i + 1] == '/') {
                    i += 2;
                    state = COMMENT_LINE;
                    continue;
                }
                if (starts_regex(out)) {
                    // Regex literal: copy until unescaped /
                    out += input[i]; // opening /
                    i++;
                    state = REGEX;
                    continue;
                }
                // Division operator — fall through to default
            }
            // Whitespace
            if (input[i] == ' ' || input[i] == '\t' || input[i] == '\n' || input[i] == '\r') {
                while (i < len && (input[i] == ' ' || input[i] == '\t' ||
                                   input[i] == '\n' || input[i] == '\r')) {
                    i++;
                }
                if (i < len && !out.empty()) {
                    char last = out.back();
                    char next = input[i];
                    bool need_space = (std::isalnum(static_cast<unsigned char>(last)) ||
                                       last == '_' || last == '$') &&
                                      (std::isalnum(static_cast<unsigned char>(next)) ||
                                       next == '_' || next == '$');
                    if (need_space) {
                        out += ' ';
                    }
                }
                continue;
            }
            out += input[i];
            i++;
            break;

        case STRING:
            out += input[i];
            if (input[i] == '\\' && i + 1 < len) {
                i++;
                out += input[i];
                i++;
                continue;
            }
            if (input[i] == quote_char) {
                state = NORMAL;
            }
            i++;
            break;

        case COMMENT_LINE:
            if (input[i] == '\n') {
                state = NORMAL;
            }
            i++;
            break;

        case COMMENT_BLOCK:
            if (i + 1 < len && input[i] == '*' && input[i + 1] == '/') {
                i += 2;
                state = NORMAL;
                continue;
            }
            i++;
            break;

        case REGEX:
            out += input[i];
            if (input[i] == '\\' && i + 1 < len) {
                i++;
                out += input[i];
                i++;
                continue;
            }
            if (input[i] == '/') {
                // Copy regex flags
                i++;
                while (i < len && std::isalpha(static_cast<unsigned char>(input[i]))) {
                    out += input[i];
                    i++;
                }
                state = NORMAL;
                continue;
            }
            i++;
            break;
        }
    }

    return out;
}

// --- Main asset processing ---

AssetResult process_assets(const Config& cfg, HashStore& hashes,
                           bool incremental,
                           std::vector<std::string>& out_asset_paths) {
    AssetResult result;

    if (!fs::exists(cfg.static_dir)) {
        return result; // no static directory — nothing to do
    }

    // Collect all files in static/
    std::vector<std::string> static_files;
    for (const auto& entry : fs::recursive_directory_iterator(cfg.static_dir)) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        if (should_skip(filename)) continue;

        static_files.push_back(entry.path().string());
    }
    std::sort(static_files.begin(), static_files.end());

    // Hash all static files
    for (const auto& f : static_files) {
        hashes.hash_file(f);
    }

    // Process each file
    for (const auto& src_path : static_files) {
        // Compute relative path from static_dir
        std::string rel;
        if (src_path.size() > cfg.static_dir.size() &&
            src_path.substr(0, cfg.static_dir.size()) == cfg.static_dir) {
            rel = src_path.substr(cfg.static_dir.size());
        } else {
            rel = src_path;
        }
        // Strip leading slash
        while (!rel.empty() && rel.front() == '/') {
            rel = rel.substr(1);
        }

        std::string dst_path = utils::path_join(cfg.output_dir, rel);
        out_asset_paths.push_back(dst_path);

        // Check if unchanged for incremental builds
        if (incremental && hashes.is_unchanged(src_path)) {
            result.files_cached++;
            continue;
        }

        std::string ext = get_extension(src_path);

        if (ext == ".css" && cfg.minify_css) {
            std::string content = utils::read_file_binary(src_path);
            std::string minified = minify_css(content);
            utils::write_file_binary(dst_path, minified);
            int saved = static_cast<int>(content.size()) - static_cast<int>(minified.size());
            if (saved > 0) result.bytes_saved += saved;
            result.files_minified++;
        } else if (ext == ".js" && cfg.minify_js) {
            std::string content = utils::read_file_binary(src_path);
            std::string minified = minify_js(content);
            utils::write_file_binary(dst_path, minified);
            int saved = static_cast<int>(content.size()) - static_cast<int>(minified.size());
            if (saved > 0) result.bytes_saved += saved;
            result.files_minified++;
        } else {
            copy_file(src_path, dst_path);
            result.files_copied++;
        }
    }

    // Remove orphaned asset outputs (files in output/ that came from a static file
    // that no longer exists)
    if (incremental && fs::exists(cfg.output_dir)) {
        std::unordered_set<std::string> active(out_asset_paths.begin(),
                                                out_asset_paths.end());
        // Check previously tracked asset paths by looking at files in output
        // that aren't in the active set
        // We limit this to only files that match the static dir structure
        // to avoid removing generated HTML pages
        for (const auto& entry : fs::recursive_directory_iterator(cfg.output_dir)) {
            if (!entry.is_regular_file()) continue;
            std::string path = entry.path().string();
            // Only consider non-.html files for asset orphan cleanup
            if (get_extension(path) == ".html") continue;
            if (active.find(path) == active.end()) {
                // Check if this file's relative path would have come from static/
                std::string rel_to_output;
                if (path.size() > cfg.output_dir.size() &&
                    path.substr(0, cfg.output_dir.size()) == cfg.output_dir) {
                    rel_to_output = path.substr(cfg.output_dir.size());
                    while (!rel_to_output.empty() && rel_to_output.front() == '/') {
                        rel_to_output = rel_to_output.substr(1);
                    }
                }
                if (!rel_to_output.empty()) {
                    std::string src_check = utils::path_join(cfg.static_dir, rel_to_output);
                    if (!fs::exists(src_check)) {
                        fs::remove(path);
                        result.files_removed++;
                    }
                }
            }
        }
    }

    return result;
}

} // namespace cstatic
