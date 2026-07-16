#include "assets/asset_pipeline.hpp"
#include "hash/hash_store.hpp"
#include "utils/path.hpp"
#include "utils/terminal.hpp"
#include "utils/file_io.hpp"

#include <xxhash.h>
#include <stb_image.h>
#include <stb_image_write.h>
#include <stb_image_resize2.h>

#include <filesystem>
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>

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

// Check if extension is an image format stb can handle.
static bool is_image_ext(const std::string& ext) {
    static const std::unordered_set<std::string> image_exts = {
        ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".tga"
    };
    return image_exts.count(ext) > 0;
}

// Compute relative path from static_dir for a full source path.
static std::string compute_rel_path(const std::string& src_path, const std::string& static_dir) {
    std::string rel;
    if (src_path.size() > static_dir.size() &&
        src_path.substr(0, static_dir.size()) == static_dir) {
        rel = src_path.substr(static_dir.size());
    } else {
        rel = src_path;
    }
    while (!rel.empty() && rel.front() == '/') {
        rel = rel.substr(1);
    }
    return rel;
}

// --- Content hashing ---

std::string content_hash_hex8(const std::string& content) {
    XXH64_hash_t hash = XXH64(content.data(), content.size(), 0);
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)hash);
    return std::string(buf, 8); // first 8 hex chars
}

// --- AssetManifest ---

std::string AssetManifest::fingerprint_path(const std::string& rel_path,
                                             const std::string& content_hash) {
    // Find the last dot to split stem from extension
    auto dot_pos = rel_path.rfind('.');
    if (dot_pos == std::string::npos) {
        return rel_path + "." + content_hash;
    }
    // Check for compound extensions like .tar.gz — but for assets, just use last dot
    return rel_path.substr(0, dot_pos) + "." + content_hash + rel_path.substr(dot_pos);
}

AssetManifest build_asset_manifest(const Config& cfg) {
    AssetManifest manifest;

    if (!fs::exists(cfg.static_dir)) {
        return manifest;
    }

    for (const auto& entry : fs::recursive_directory_iterator(cfg.static_dir)) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        if (should_skip(filename)) continue;

        std::string src_path = entry.path().string();
        std::string rel = compute_rel_path(src_path, cfg.static_dir);

        std::string content = utils::read_file_binary(src_path);
        std::string hash = content_hash_hex8(content);

        manifest.entries[rel] = AssetManifest::fingerprint_path(rel, hash);
    }

    return manifest;
}

// --- External tool detection (WebP/AVIF) ---

static bool tool_available(const std::string& tool) {
#ifdef _WIN32
    std::string cmd = "where " + tool + " >nul 2>nul";
#else
    std::string cmd = "which " + tool + " >/dev/null 2>&1";
#endif
    return std::system(cmd.c_str()) == 0;
}

// One-time tool availability cache.
struct ToolCache {
    bool cwebp_checked = false;
    bool cwebp_available = false;
    bool avifenc_checked = false;
    bool avifenc_available = false;
    bool cwebp_warned = false;
    bool avifenc_warned = false;
};

static ToolCache& get_tool_cache() {
    static ToolCache cache;
    return cache;
}

// Run an external converter on the input file, writing output to dst_path.
static bool run_converter(const std::string& tool, const std::string& input_path,
                          const std::string& output_path, int quality) {
    std::ostringstream cmd;
    if (tool == "cwebp") {
        cmd << "cwebp -q " << quality << " \"" << input_path << "\" -o \"" << output_path << "\" 2>/dev/null";
    } else if (tool == "avifenc") {
        cmd << "avifenc --quality " << quality << " \"" << input_path << "\" \"" << output_path << "\" 2>/dev/null";
    } else {
        return false;
    }
    return std::system(cmd.str().c_str()) == 0;
}

// Convert image to WebP/AVIF format by writing temp file and running external tool.
static void convert_image(const std::string& content, const std::string& dst_base_path,
                           const std::string& format_ext, const std::string& tool,
                           int quality, AssetResult& result) {
    auto& cache = get_tool_cache();

    bool available = false;
    if (tool == "cwebp") {
        if (!cache.cwebp_checked) {
            cache.cwebp_available = tool_available("cwebp");
            cache.cwebp_checked = true;
        }
        available = cache.cwebp_available;
        if (!available && !cache.cwebp_warned) {
            std::cerr << utils::notice_label() << " cwebp not found on PATH — skipping WebP conversion\n";
            cache.cwebp_warned = true;
        }
    } else if (tool == "avifenc") {
        if (!cache.avifenc_checked) {
            cache.avifenc_available = tool_available("avifenc");
            cache.avifenc_checked = true;
        }
        available = cache.avifenc_available;
        if (!available && !cache.avifenc_warned) {
            std::cerr << utils::notice_label() << " avifenc not found on PATH — skipping AVIF conversion\n";
            cache.avifenc_warned = true;
        }
    }

    if (!available) return;

    // Find the original (non-fingerprinted) output path to use as temp input
    // for the converter. Write content to a temp file.
    std::string tmp_path = dst_base_path + ".tmp_convert";
    utils::write_file_binary(tmp_path, content);

    std::string out_path = dst_base_path + format_ext;
    bool ok = run_converter(tool, tmp_path, out_path, quality);

    // Clean up temp file
    std::remove(tmp_path.c_str());

    if (ok) {
        result.images_converted++;
    }
}

// --- Image optimization ---

// stb_image_write callback: writes to a std::string buffer.
struct StbWriteContext {
    std::string* buffer;
    size_t offset;
};

static void stb_write_to_string(void* context, void* data, int size) {
    auto* ctx = static_cast<StbWriteContext*>(context);
    ctx->buffer->append(static_cast<char*>(data), size);
}

std::string optimize_image(const std::string& input_bytes,
                           int max_width, int quality,
                           const std::string& ext, int& bytes_saved) {
    bytes_saved = 0;

    int w, h, channels;
    unsigned char* data = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(input_bytes.data()),
        static_cast<int>(input_bytes.size()),
        &w, &h, &channels, 0);

    if (!data) {
        return ""; // unsupported or corrupt image
    }

    // Always output as either JPEG or PNG (matching input format).
    bool output_jpeg = (ext == ".jpg" || ext == ".jpeg");

    unsigned char* final_data = data;
    int final_w = w;
    int final_h = h;
    unsigned char* resized_data = nullptr;

    // Resize if wider than max_width
    if (w > max_width) {
        int new_w = max_width;
        int new_h = (h * max_width) / w;
        if (new_h < 1) new_h = 1;

        resized_data = (unsigned char*)malloc(new_w * new_h * channels);
        if (resized_data) {
            stbir_resize_uint8_linear(data, w, h, 0,
                                       resized_data, new_w, new_h, 0,
                                       static_cast<stbir_pixel_layout>(channels));
            final_data = resized_data;
            final_w = new_w;
            final_h = new_h;
        }
    }

    // Encode output
    std::string output;
    StbWriteContext ctx{&output, 0};

    if (output_jpeg) {
        int q = quality;
        if (q < 1) q = 1;
        if (q > 100) q = 100;
        stbi_write_jpg_to_func(stb_write_to_string, &ctx,
                                final_w, final_h, channels, final_data, q);
    } else {
        // PNG — stride = width * channels
        stbi_write_png_to_func(stb_write_to_string, &ctx,
                                final_w, final_h, channels, final_data,
                                final_w * channels);
    }

    if (resized_data) {
        free(resized_data);
    }
    stbi_image_free(data);

    bytes_saved = static_cast<int>(input_bytes.size()) - static_cast<int>(output.size());

    return output;
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

// --- HTML Minification ---

static bool is_optional_closing_tag(const std::string& tag) {
    static const std::unordered_set<std::string> optional_tags = {
        "li", "p", "dt", "dd", "tr", "td", "th",
        "thead", "tbody", "tfoot", "option", "colgroup"
    };
    std::string lower = tag;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return optional_tags.count(lower) > 0;
}

std::string minify_html(const std::string& input) {
    if (input.empty()) return input;

    enum State { NORMAL, TAG, COMMENT };

    std::string out;
    out.reserve(input.size());

    size_t i = 0;
    size_t len = input.size();
    State state = NORMAL;

    while (i < len) {
        switch (state) {
        case NORMAL: {
            // HTML comment
            if (i + 3 < len && input[i] == '<' && input[i+1] == '!' &&
                input[i+2] == '-' && input[i+3] == '-') {
                // Check for conditional comment: <!--[if ...]>
                if (i + 6 < len && input[i+4] == '[' && input[i+5] == 'i' && input[i+6] == 'f') {
                    // Preserve entire conditional comment
                    out += "<!--";
                    i += 4;
                    // Find <![endif]-->
                    while (i + 11 < len) {
                        if (input[i] == '<' && input[i+1] == '!' &&
                            input[i+2] == '[' && input[i+3] == 'e' &&
                            input[i+4] == 'n' && input[i+5] == 'd' &&
                            input[i+6] == 'i' && input[i+7] == 'f' &&
                            input[i+8] == ']') {
                            // Copy from current position through -->
                            out += input.substr(i, 12); // <![endif]-->
                            i += 12;
                            break;
                        }
                        out += input[i];
                        i++;
                    }
                } else {
                    // Regular comment — skip until -->
                    i += 4;
                    while (i + 2 < len && !(input[i] == '-' && input[i+1] == '-' && input[i+2] == '>')) {
                        i++;
                    }
                    i += 3; // skip -->
                }
                continue;
            }

            // Opening tag
            if (input[i] == '<') {
                out += input[i];
                i++;
                state = TAG;
                continue;
            }

            // Collapse whitespace
            if (input[i] == ' ' || input[i] == '\t' || input[i] == '\n' || input[i] == '\r') {
                while (i < len && (input[i] == ' ' || input[i] == '\t' ||
                                   input[i] == '\n' || input[i] == '\r')) {
                    i++;
                }
                // Emit single space only if it's between two non-tag characters
                if (!out.empty() && i < len && input[i] != '<') {
                    out += ' ';
                }
                continue;
            }

            out += input[i];
            i++;
            break;
        }

        case TAG: {
            out += input[i];

            // Check for closing tag with optional closing tag name
            if (input[i] == '/' && out.size() >= 2 && out[out.size()-2] == '<') {
                // Collect the tag name
                i++;
                std::string tag_name;
                while (i < len && input[i] != '>' && input[i] != ' ' && input[i] != '\t') {
                    tag_name += input[i];
                    i++;
                }
                if (is_optional_closing_tag(tag_name)) {
                    // Skip to end of closing tag
                    while (i < len && input[i] != '>') {
                        i++;
                    }
                    i++; // skip >
                    // Remove the </ we already wrote
                    out.resize(out.size() - 2);
                    state = NORMAL;
                    continue;
                }
                // Not optional — write the tag name
                out += tag_name;
                continue;
            }

            // Attribute quote removal: after = inside tag, if value is a simple word
            if (input[i] == '=') {
                // Look ahead to see if next non-space char is a quote
                size_t peek = i + 1;
                while (peek < len && (input[peek] == ' ' || input[peek] == '\t')) {
                    peek++;
                }
                if (peek < len && (input[peek] == '"' || input[peek] == '\'')) {
                    char quote = input[peek];
                    // Scan the value inside quotes
                    size_t val_start = peek + 1;
                    size_t val_end = val_start;
                    bool is_simple = true;
                    while (val_end < len && input[val_end] != quote) {
                        char c = input[val_end];
                        if (!std::isalnum(static_cast<unsigned char>(c)) &&
                            c != '-' && c != '_' && c != '.') {
                            is_simple = false;
                        }
                        val_end++;
                    }
                    if (is_simple && val_end > val_start && val_end < len && input[val_end] == quote) {
                        // Remove quotes: write the value directly
                        out.append(input.substr(val_start, val_end - val_start));
                        i = val_end + 1;
                        continue;
                    }
                }
            }

            if (input[i] == '>') {
                state = NORMAL;
            }
            i++;
            break;
        }

        case COMMENT:
            // Should not reach here — handled in NORMAL
            i++;
            break;
        }
    }

    return out;
}

// --- Main asset processing ---

AssetResult process_assets(const Config& cfg, HashStore& hashes,
                           bool incremental,
                           std::vector<std::string>& out_asset_paths,
                           AssetManifest* manifest) {
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
        std::string rel = compute_rel_path(src_path, cfg.static_dir);

        std::string ext = get_extension(src_path);

        // Read the file content (needed for minification, image processing, fingerprinting)
        std::string content = utils::read_file_binary(src_path);

        // Compute fingerprinted path if needed
        std::string output_rel = rel;
        std::string hash_str;
        if (manifest) {
            hash_str = content_hash_hex8(content);
            output_rel = AssetManifest::fingerprint_path(rel, hash_str);
        }

        std::string dst_path = utils::path_join(cfg.output_dir, output_rel);
        out_asset_paths.push_back(dst_path);

        // Also write to original path for backwards compat when fingerprinting
        std::string orig_dst_path;
        if (manifest) {
            orig_dst_path = utils::path_join(cfg.output_dir, rel);
            out_asset_paths.push_back(orig_dst_path);
        }

        // Check if unchanged for incremental builds
        if (incremental && hashes.is_unchanged(src_path)) {
            result.files_cached++;
            continue;
        }

        // Process based on type
        if (ext == ".css" && cfg.minify_css) {
            std::string processed = minify_css(content);
            int saved = static_cast<int>(content.size()) - static_cast<int>(processed.size());

            // Update fingerprint hash based on processed content
            if (manifest && saved > 0) {
                hash_str = content_hash_hex8(processed);
                output_rel = AssetManifest::fingerprint_path(rel, hash_str);
                dst_path = utils::path_join(cfg.output_dir, output_rel);
                // Update the first entry in out_asset_paths
                if (!out_asset_paths.empty()) {
                    out_asset_paths[out_asset_paths.size() - 2] = dst_path;
                }
            }

            utils::write_file_binary(dst_path, processed);
            if (manifest) {
                utils::write_file_binary(orig_dst_path, processed);
            }
            if (saved > 0) result.bytes_saved += saved;
            result.files_minified++;

        } else if (ext == ".js" && cfg.minify_js) {
            std::string processed = minify_js(content);
            int saved = static_cast<int>(content.size()) - static_cast<int>(processed.size());

            if (manifest && saved > 0) {
                hash_str = content_hash_hex8(processed);
                output_rel = AssetManifest::fingerprint_path(rel, hash_str);
                dst_path = utils::path_join(cfg.output_dir, output_rel);
                if (!out_asset_paths.empty()) {
                    out_asset_paths[out_asset_paths.size() - 2] = dst_path;
                }
            }

            utils::write_file_binary(dst_path, processed);
            if (manifest) {
                utils::write_file_binary(orig_dst_path, processed);
            }
            if (saved > 0) result.bytes_saved += saved;
            result.files_minified++;

        } else if (is_image_ext(ext) && cfg.images_optimize) {
            int img_saved = 0;
            std::string optimized = optimize_image(content, cfg.images_max_width,
                                                     cfg.images_quality, ext, img_saved);

            if (!optimized.empty()) {
                if (img_saved > 0) {
                    result.bytes_saved += img_saved;
                    result.images_optimized++;

                    // Update fingerprint hash based on optimized content
                    if (manifest) {
                        hash_str = content_hash_hex8(optimized);
                        output_rel = AssetManifest::fingerprint_path(rel, hash_str);
                        dst_path = utils::path_join(cfg.output_dir, output_rel);
                        if (!out_asset_paths.empty()) {
                            out_asset_paths[out_asset_paths.size() - 2] = dst_path;
                        }
                    }
                }

                utils::write_file_binary(dst_path, optimized);
                if (manifest) {
                    utils::write_file_binary(orig_dst_path, optimized);
                }

                // WebP conversion
                if (cfg.images_webp) {
                    // Use the original (non-fingerprinted) output as the base path for conversion
                    std::string webp_base = manifest ? orig_dst_path : dst_path;
                    convert_image(optimized, webp_base, ".webp", "cwebp",
                                 cfg.images_quality, result);
                }
                // AVIF conversion
                if (cfg.images_avif) {
                    std::string avif_base = manifest ? orig_dst_path : dst_path;
                    convert_image(optimized, avif_base, ".avif", "avifenc",
                                 cfg.images_quality, result);
                }
            } else {
                // Unsupported format — copy as-is
                utils::write_file_binary(dst_path, content);
                if (manifest) {
                    utils::write_file_binary(orig_dst_path, content);
                }
                result.files_copied++;
            }

        } else {
            // All other files: copy as-is
            utils::write_file_binary(dst_path, content);
            if (manifest) {
                utils::write_file_binary(orig_dst_path, content);
            }
            result.files_copied++;
        }

        // Add to manifest
        if (manifest) {
            manifest->entries[rel] = output_rel;
        }
    }

    // Remove orphaned asset outputs (files in output/ that came from a static file
    // that no longer exists)
    if (incremental && fs::exists(cfg.output_dir)) {
        std::unordered_set<std::string> active(out_asset_paths.begin(),
                                                out_asset_paths.end());
        for (const auto& entry : fs::recursive_directory_iterator(cfg.output_dir)) {
            if (!entry.is_regular_file()) continue;
            std::string path = entry.path().string();
            // Only consider non-.html files for asset orphan cleanup
            if (get_extension(path) == ".html") continue;
            // G15: skip generated markdown mirror files (named exactly
            // "index" + mirror suffix) — they're managed by the builder, not
            // the asset pipeline. User-authored .md assets stay cleanable:
            // they're in `active` when present, and only the generator form
            // is skipped here.
            if (cfg.markdown_mirror_enabled && !cfg.markdown_mirror_suffix.empty() &&
                entry.path().filename().string() == "index" + cfg.markdown_mirror_suffix) {
                continue;
            }
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

    // Write manifest.json to output directory
    if (manifest && !manifest->entries.empty()) {
        std::ostringstream json;
        json << "{\n";
        bool first = true;
        for (const auto& [orig, fp] : manifest->entries) {
            if (!first) json << ",\n";
            first = false;
            json << "  \"" << orig << "\": \"" << fp << "\"";
        }
        json << "\n}\n";

        std::string manifest_path = utils::path_join(cfg.output_dir, "manifest.json");
        utils::write_file_binary(manifest_path, json.str());
    }

    return result;
}

// Backward-compatible overload without manifest.
AssetResult process_assets(const Config& cfg, HashStore& hashes,
                           bool incremental,
                           std::vector<std::string>& out_asset_paths) {
    return process_assets(cfg, hashes, incremental, out_asset_paths, nullptr);
}

} // namespace cstatic
