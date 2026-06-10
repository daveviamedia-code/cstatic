#pragma once

#include "config/config.hpp"
#include <string>
#include <vector>

namespace cstatic {

struct Page; // forward declaration from template/renderer.hpp

// Result of a build run.
struct BuildResult {
    int pages_built = 0;
    int pages_cached = 0;    // unchanged pages (incremental hit)
    int pages_removed = 0;   // orphaned outputs cleaned up
    int pages_skipped = 0;   // drafts
    int assets_copied = 0;
    int assets_minified = 0;
    int assets_cached = 0;
    int assets_removed = 0;
    int bytes_saved = 0;     // bytes saved by minification
    double elapsed_ms = 0;
};

// Run the full build pipeline.
// If full_rebuild is true, ignore the hash cache and rebuild everything.
BuildResult build_site(const Config& cfg, bool full_rebuild = false);

} // namespace cstatic
