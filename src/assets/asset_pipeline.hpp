#pragma once

#include "config/config.hpp"
#include <string>
#include <vector>
#include <map>

namespace cstatic {

class HashStore; // forward declaration

// Result of asset processing.
struct AssetResult {
    int files_copied = 0;    // static files copied as-is
    int files_minified = 0;  // CSS/JS files minified
    int files_cached = 0;    // unchanged assets skipped (incremental)
    int files_removed = 0;   // orphaned output assets cleaned up
    int bytes_saved = 0;     // bytes saved by minification
    int images_optimized = 0; // images resized/recompressed
    int images_converted = 0; // images converted to WebP/AVIF
};

// Asset manifest: maps original relative paths to fingerprinted paths.
// e.g. "css/style.css" -> "css/style.a3f7b2c1.css"
struct AssetManifest {
    std::map<std::string, std::string> entries;

    // Compute fingerprinted filename for a given relative path.
    // Uses first 8 hex chars of XXH64 hash of file contents.
    static std::string fingerprint_path(const std::string& rel_path,
                                        const std::string& content_hash);
};

// Process all static assets: copy to output, optionally minify CSS/JS.
// Integrates with the hash store for incremental builds.
// Returns the output paths of all processed assets (for orphan tracking).
AssetResult process_assets(const Config& cfg, HashStore& hashes,
                           bool incremental,
                           std::vector<std::string>& out_asset_paths);

// Process all static assets with optional fingerprinting.
// If manifest is non-null, fingerprinted filenames are computed and manifest is populated.
AssetResult process_assets(const Config& cfg, HashStore& hashes,
                           bool incremental,
                           std::vector<std::string>& out_asset_paths,
                           AssetManifest* manifest);

// --- Minification functions (public for testing) ---

// Minify CSS content: remove comments, collapse whitespace, trim semicolons.
std::string minify_css(const std::string& input);

// Minify JS content: remove comments, collapse whitespace, preserve strings.
std::string minify_js(const std::string& input);

// Minify HTML content: remove comments, collapse whitespace, remove optional closing tags.
std::string minify_html(const std::string& input);

// --- Image optimization (public for testing) ---

// Optimize an image: resize if needed, apply quality. Returns optimized bytes.
// Returns empty string if format is unsupported.
// ext should include the dot (e.g. ".jpg", ".png").
// bytes_saved is set to the difference between input and output sizes.
std::string optimize_image(const std::string& input_bytes,
                           int max_width, int quality,
                           const std::string& ext, int& bytes_saved);

// Compute XXH64 hash of content, return first 8 hex chars as a string.
std::string content_hash_hex8(const std::string& content);

// Pre-compute asset manifest by scanning the static directory.
AssetManifest build_asset_manifest(const Config& cfg);

} // namespace cstatic
