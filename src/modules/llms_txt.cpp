#include "modules/llms_txt.hpp"
#include "config/config.hpp"
#include "utils/file_io.hpp"
#include "utils/glob.hpp"

#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

namespace cstatic {
namespace modules {

namespace {

// Collapse any run of whitespace (incl. newlines) into a single space so each
// llms.txt entry stays on one line; trims leading/trailing space.
std::string collapse_ws(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool prev_ws = false;
    for (char c : s) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ' || c == '\v' || c == '\f') {
            if (!prev_ws) { out.push_back(' '); prev_ws = true; }
        } else {
            out.push_back(c);
            prev_ws = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    size_t start = 0;
    while (start < out.size() && out[start] == ' ') ++start;
    return start > 0 ? out.substr(start) : out;
}

// Write the `## Pages` listing for `pages` (already filtered/sorted upstream)
// into a single string, applying the `max_pages` cap (0 = no cap). Pages with
// empty url/title or matching an exclude glob are skipped and do not count
// against the cap.
std::string render_page_list(const Config& cfg, const nlohmann::json& pages,
                             int max_pages) {
    std::ostringstream out;
    int emitted = 0;
    for (const auto& page : pages) {
        if (max_pages > 0 && emitted >= max_pages) break;

        std::string title = collapse_ws(page.value("title", ""));
        std::string url   = page.value("url", "");
        if (title.empty() || url.empty()) continue;
        if (utils::matches_any_glob(url, cfg.llms_txt_exclude)) continue;

        std::string excerpt = utils::truncate_text(
            collapse_ws(page.value("excerpt", "")), 160);

        out << "- [" << title << "](" << cfg.site_base_url << url << ")";
        if (!excerpt.empty()) {
            out << ": " << excerpt;
        }
        out << "\n";
        ++emitted;
    }
    return out.str();
}

} // anonymous namespace

void generate_llms_txt(const Config& cfg, const nlohmann::json& pages,
                       const std::string& output_dir) {
    const std::string& title = cfg.site_title;
    const std::string description = !cfg.llms_txt_description.empty()
        ? cfg.llms_txt_description
        : cfg.site_description;

    auto write_file = [&](const std::string& filename, int max_pages) {
        std::ostringstream out;
        out << "# " << title << "\n\n";
        if (!description.empty()) {
            out << "> " << description << "\n\n";
        }
        out << "## Pages:\n";
        out << render_page_list(cfg, pages, max_pages);
        utils::write_file(output_dir + "/" + filename, out.str());
    };

    // Compact catalog honors the cap; full catalog is never capped.
    write_file("llms.txt", cfg.llms_txt_max_pages);
    write_file("llms-full.txt", 0);
}

} // namespace modules
} // namespace cstatic
