#pragma once

#include "pipeline/builder.hpp"
#include <string>

namespace cstatic::cli {

// Format a BuildError for human-readable display in the error summary.
//
// For errors with line > 0 and a resolvable source file, includes +/-3 lines
// of source context with a '>' marker on the offending line. When column > 0,
// emits a caret ('^') line pointing at the column.
//
// `template_dir` resolves the template file for Template errors; other error
// types read directly from err.source_file.
std::string format_build_error(const cstatic::BuildError& err,
                               const std::string& template_dir);

} // namespace cstatic::cli
