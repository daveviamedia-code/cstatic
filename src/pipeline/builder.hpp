#pragma once

#include "config/config.hpp"
#include <string>
#include <vector>

namespace cstatic {

struct Page; // forward declaration from template/renderer.hpp

// A structured error collected during a build instead of throwing on first error.
struct BuildError {
    enum class Type { Template, Frontmatter, Markdown, Generic };
    Type type = Type::Generic;
    std::string source_file;    // e.g. "src/posts/hello.md"
    std::string template_name;  // e.g. "post" (empty if N/A)
    int line = 0;               // 0 if unknown
    std::string message;
};

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
    std::vector<BuildError> errors;  // collected errors (non-fatal)
};

// Run the full build pipeline.
// If full_rebuild is true, ignore the hash cache and rebuild everything.
BuildResult build_site(const Config& cfg, bool full_rebuild = false, bool include_drafts = false, int jobs = 0);

} // namespace cstatic
