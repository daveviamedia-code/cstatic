#include "cli/error_format.hpp"
#include "utils/path.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace cstatic::cli {

namespace {

// Resolve the on-disk file to read for source context.
// Template errors point at <template_dir>/<template_name>.html; everything
// else (frontmatter, markdown, generic) points at the source file directly.
std::string resolve_source_file(const cstatic::BuildError& err,
                                const std::string& template_dir) {
    if (!err.template_name.empty()) {
        return cstatic::utils::path_join(template_dir, err.template_name + ".html");
    }
    return err.source_file;
}

} // namespace

std::string format_build_error(const cstatic::BuildError& err,
                               const std::string& template_dir) {
    std::ostringstream out;

    const char* type_str = "error";
    switch (err.type) {
        case cstatic::BuildError::Type::Template:    type_str = "template"; break;
        case cstatic::BuildError::Type::Frontmatter: type_str = "frontmatter"; break;
        case cstatic::BuildError::Type::Markdown:    type_str = "markdown"; break;
        default: break;
    }

    if (!err.source_file.empty()) {
        out << err.source_file;
    }

    if (!err.template_name.empty()) {
        out << ": " << type_str << " '" << err.template_name << "'";
        if (err.line > 0) {
            std::string tmpl_path = cstatic::utils::path_join(template_dir, err.template_name + ".html");
            out << " (" << tmpl_path << ":" << err.line << ")";
        }
    } else {
        out << ": " << type_str << " error";
        if (err.line > 0) {
            out << " (line " << err.line;
            if (err.column > 0) out << ", col " << err.column;
            out << ")";
        }
    }

    out << "\n    " << err.message;

    // Source context for any error that carries a line number and a resolvable
    // file. Shows +/-3 lines with a '>' marker on the offending line.
    if (err.line > 0) {
        std::string src_path = resolve_source_file(err, template_dir);
        if (!src_path.empty()) {
            std::ifstream f(src_path);
            if (f.is_open()) {
                std::vector<std::string> lines;
                std::string line;
                while (std::getline(f, line)) {
                    lines.push_back(line);
                }
                int target = err.line;
                int start_line = std::max(1, target - 3);
                int end_line = std::min(static_cast<int>(lines.size()), target + 3);
                bool emitted_any = false;
                for (int l = start_line; l <= end_line; l++) {
                    const char* marker = (l == target) ? ">" : " ";
                    out << "\n    " << marker << " " << l << " | "
                        << (l <= static_cast<int>(lines.size()) ? lines[l - 1] : "");
                    emitted_any = true;
                }
                // Caret line pointing at the column (aligned under the target line).
                if (emitted_any && err.column > 0) {
                    std::string gutter = "    > " + std::to_string(target) + " | ";
                    out << "\n" << std::string(gutter.size(), ' ');
                    for (int c = 1; c < err.column; c++) out << ' ';
                    out << "^";
                }
            }
        }
    }

    return out.str();
}

} // namespace cstatic::cli
