#include "content/markdown.hpp"

#include <cmark.h>
#include <string>

namespace cstatic {

std::string render_markdown(const std::string& markdown) {
    // CMARK_OPT_UNSAFE allows raw HTML passthrough in markdown
    // CMARK_OPT_SMART enables smart typography (em dashes, curly quotes)
    int options = CMARK_OPT_UNSAFE | CMARK_OPT_SMART;
    char* html = cmark_markdown_to_html(markdown.c_str(), markdown.size(), options);
    std::string result(html);
    free(html);
    return result;
}

} // namespace cstatic
