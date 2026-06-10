#pragma once

#include <string>

namespace cstatic {

// Render CommonMark Markdown to HTML using cmark.
std::string render_markdown(const std::string& markdown);

} // namespace cstatic
