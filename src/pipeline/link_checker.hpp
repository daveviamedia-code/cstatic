#pragma once

#include <string>
#include <vector>

namespace cstatic {
namespace pipeline {

// A single broken-link finding reported by check_links().
struct LinkIssue {
    std::string source_file;  // absolute path to .html containing the link
    int         line = 0;     // 1-based line number
    std::string href;         // raw attribute value
    std::string message;      // human-readable description
    bool        is_external = false;
};

// Aggregate result of a check_links() run.
struct CheckResult {
    int total_links       = 0;  // every href/src encountered
    int internal_checked  = 0;  // internal links verified against the filesystem
    int external_checked  = 0;  // unique external URLs hit via HTTP
    std::vector<LinkIssue> issues;
};

// Scan every .html file under output_dir and verify its links.
// - Internal links (absolute paths starting with '/') are resolved against
//   output_dir and checked for existence.
// - External links are skipped unless check_external is true, in which case
//   each unique URL is probed once via HTTP HEAD (GET fallback), following up
//   to 3 redirects, with the given per-request timeout.
// Transport-level failures (DNS, connection refused, timeout, missing HTTPS
// support) are reported as warnings on stderr and do NOT count as issues;
// HTTP error statuses (>= 400) and missing internal files DO count.
CheckResult check_links(const std::string& output_dir,
                        bool check_external = false,
                        int  timeout_ms     = 5000);

} // namespace pipeline
} // namespace cstatic
