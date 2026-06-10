#pragma once

#include "config/config.hpp"
#include <string>
#include <vector>

namespace cstatic {

class HashStore; // forward declaration

// Result of asset processing.
struct AssetResult {
    int files_copied = 0;    // static files copied as-is
    int files_minified = 0;  // CSS/JS files minified
    int files_cached = 0;    // unchanged assets skipped (incremental)
    int files_removed = 0;   // orphaned output assets cleaned up
    int bytes_saved = 0;     // bytes saved by minification
};

// Process all static assets: copy to output, optionally minify CSS/JS.
// Integrates with the hash store for incremental builds.
// Returns the output paths of all processed assets (for orphan tracking).
AssetResult process_assets(const Config& cfg, HashStore& hashes,
                           bool incremental,
                           std::vector<std::string>& out_asset_paths);

// --- Minification functions (public for testing) ---

// Minify CSS content: remove comments, collapse whitespace, trim semicolons.
std::string minify_css(const std::string& input);

// Minify JS content: remove comments, collapse whitespace, preserve strings.
std::string minify_js(const std::string& input);

} // namespace cstatic
