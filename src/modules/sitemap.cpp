#include "modules/sitemap.hpp"
#include "config/config.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace cstatic {
namespace modules {

namespace {

void write_file(const std::string& path, const std::string& content) {
    namespace fs = std::filesystem;
    auto dir = fs::path(path).parent_path();
    if (!dir.empty() && !fs::exists(dir)) {
        fs::create_directories(dir);
    }
    std::ofstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("cannot write file: " + path);
    }
    f << content;
}

// Simple glob match supporting '*' (any chars) only.
bool glob_match(const std::string& pattern, const std::string& text) {
    auto pit = pattern.begin();
    auto tit = text.begin();

    while (pit != pattern.end() && tit != text.end()) {
        if (*pit == '*') {
            ++pit;
            // '*' matches zero or more of any character
            if (pit == pattern.end()) return true; // trailing * matches rest
            // Find the next occurrence of the char after *
            while (tit != text.end() && *tit != *pit) ++tit;
        } else {
            if (*pit != *tit) return false;
            ++pit;
            ++tit;
        }
    }
    // Consume trailing wildcards
    while (pit != pattern.end() && *pit == '*') ++pit;
    return pit == pattern.end() && tit == text.end();
}

bool is_excluded(const std::string& url, const std::vector<std::string>& patterns) {
    for (const auto& pat : patterns) {
        if (glob_match(pat, url)) return true;
    }
    return false;
}

std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

} // anonymous namespace

void generate_sitemap(const Config& cfg, const nlohmann::json& pages,
                      const std::string& output_dir) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";

    int count = 0;
    for (const auto& page : pages) {
        std::string url = page.value("url", "");
        if (url.empty()) continue;
        if (is_excluded(url, cfg.sitemap_exclude)) continue;

        xml << "  <url>\n";
        xml << "    <loc>" << xml_escape(cfg.site_base_url + url) << "</loc>\n";

        std::string date = page.value("date", "");
        if (!date.empty()) {
            xml << "    <lastmod>" << xml_escape(date) << "</lastmod>\n";
        }

        xml << "  </url>\n";
        count++;
    }

    xml << "</urlset>\n";

    std::string path = output_dir + "/sitemap.xml";
    write_file(path, xml.str());
}

} // namespace modules
} // namespace cstatic
