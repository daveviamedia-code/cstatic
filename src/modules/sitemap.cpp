#include "modules/sitemap.hpp"
#include "config/config.hpp"
#include "utils/file_io.hpp"
#include "utils/glob.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <sstream>

namespace cstatic {
namespace modules {

namespace {

bool is_excluded(const std::string& url, const std::vector<std::string>& patterns) {
    return utils::matches_any_glob(url, patterns);
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
        xml << "    <loc>" << utils::xml_escape(cfg.site_base_url + url) << "</loc>\n";

        std::string date = page.value("date", "");
        if (!date.empty()) {
            xml << "    <lastmod>" << utils::xml_escape(date) << "</lastmod>\n";
        }

        std::string changefreq = page.value("sitemap_changefreq", "");
        if (!changefreq.empty()) {
            xml << "    <changefreq>" << utils::xml_escape(changefreq) << "</changefreq>\n";
        }
        std::string priority = page.value("sitemap_priority", "");
        if (!priority.empty()) {
            xml << "    <priority>" << utils::xml_escape(priority) << "</priority>\n";
        }

        xml << "  </url>\n";
        count++;
    }

    xml << "</urlset>\n";

    std::string path = output_dir + "/sitemap.xml";
    utils::write_file(path, xml.str());
}

} // namespace modules
} // namespace cstatic
