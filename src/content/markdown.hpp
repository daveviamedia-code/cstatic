#pragma once

#include <string>
#include <vector>

namespace cstatic {

// Options controlling Markdown rendering.
struct MarkdownOptions {
    bool        highlight_enabled = false;
    std::string highlight_style   = "github";
    // GFM extensions to enable. Empty = all four enabled (table, tasklist,
    // strikethrough, autolink). Otherwise only the listed extensions are on.
    std::vector<std::string> extensions;
};

// Render CommonMark Markdown to HTML using cmark-gfm.
// (Convenience overload: no extensions, no highlighting.)
std::string render_markdown(const std::string& markdown);

// Render Markdown to HTML with the given options (extensions + highlighting).
std::string render_markdown(const std::string& markdown, const MarkdownOptions& opts);

// Apply syntax highlighting to <pre><code class="language-X"> blocks in HTML.
// Tokens are wrapped in <span class="hl-*">. Unknown languages are left as-is.
// `style` selects the CSS theme name (kept for API symmetry; the function
// itself is style-independent — the theme only affects highlight_css()).
std::string highlight_code_blocks(const std::string& html, const std::string& style);

// Return the highlight CSS for a named style.
// Supported: "github" (light), "github-dark". Unknown styles fall back to github.
std::string highlight_css(const std::string& style);

} // namespace cstatic
