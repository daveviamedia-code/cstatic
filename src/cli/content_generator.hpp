#pragma once

#include <string>

namespace cstatic::cli {

// Generate a new content file from an archetype.
//
//   target_file    - where to write (e.g. "src/posts/hello.md"); parent
//                    directories are created as needed. Existing files are
//                    NOT overwritten.
//   archetypes_dir - directory holding "<kind>.md" templates (e.g.
//                    "archetypes"). Need not exist; falls back to a built-in.
//   kind           - archetype name; empty, missing, or "default" resolves to
//                    "default". An unknown kind also falls back to "default".
//   today          - date string substituted into {{ date }}, formatted
//                    YYYY-MM-DD. Empty -> use the current local date.
//
// Substitutes these placeholders in the archetype body:
//   {{ title }} -> human-readable title derived from the filename stem
//                  (hyphens/underscores -> spaces, Title-Cased)
//   {{ slug }}  -> the filename stem verbatim (e.g. "my-post")
//   {{ date }}  -> today (or the explicit value)
//
// Prints a "created" line on success and an "error:" message on failure.
// Returns 0 on success, non-zero exit code on any error (target exists,
// archetype unreadable, write failure).
int generate_content(const std::string& target_file,
                     const std::string& archetypes_dir,
                     const std::string& kind,
                     const std::string& today = "");

} // namespace cstatic::cli
