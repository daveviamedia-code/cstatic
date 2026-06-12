#include "modules/rss.hpp"
#include "config/config.hpp"
#include "utils/file_io.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <sstream>
#include <ctime>
#include <iomanip>

namespace cstatic {
namespace modules {

namespace {

// Convert ISO date string ("2024-01-15") to RFC 822 format ("Mon, 15 Jan 2024 00:00:00 +0000").
std::string iso_to_rfc822(const std::string& iso_date) {
    if (iso_date.empty()) return "";

    std::tm tm = {};
    std::istringstream ss(iso_date);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (ss.fail()) return "";

    char buf[64];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y 00:00:00 +0000", &tm);
    return std::string(buf);
}

} // anonymous namespace

void generate_rss(const Config& cfg, const nlohmann::json& pages,
                  const std::string& output_dir) {
    std::string title = cfg.rss_title.empty() ? cfg.site_title : cfg.rss_title;
    std::string description = cfg.rss_description.empty()
        ? "Feed for " + cfg.site_title
        : cfg.rss_description;
    std::string link = cfg.site_base_url;

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<rss version=\"2.0\">\n";
    xml << "  <channel>\n";
    xml << "    <title>" << utils::xml_escape(title) << "</title>\n";
    xml << "    <link>" << utils::xml_escape(link) << "</link>\n";
    xml << "    <description>" << utils::xml_escape(description) << "</description>\n";
    xml << "    <language>" << utils::xml_escape(cfg.site_language) << "</language>\n";

    // pages_array is already sorted by date descending
    int count = 0;
    for (const auto& page : pages) {
        if (count >= cfg.rss_item_count) break;

        std::string page_title = page.value("title", "");
        std::string page_url = page.value("url", "");
        std::string page_date = page.value("date", "");
        std::string page_excerpt = page.value("excerpt", "");

        if (page_url.empty() || page_title.empty()) continue;

        xml << "    <item>\n";
        xml << "      <title>" << utils::xml_escape(page_title) << "</title>\n";
        xml << "      <link>" << utils::xml_escape(cfg.site_base_url + page_url) << "</link>\n";
        if (!page_excerpt.empty()) {
            xml << "      <description>" << utils::xml_escape(page_excerpt) << "</description>\n";
        }
        if (!page_date.empty()) {
            xml << "      <pubDate>" << iso_to_rfc822(page_date) << "</pubDate>\n";
        }
        xml << "      <guid>" << utils::xml_escape(cfg.site_base_url + page_url) << "</guid>\n";
        xml << "    </item>\n";

        count++;
    }

    xml << "  </channel>\n";
    xml << "</rss>\n";

    std::string path = output_dir + "/feed.xml";
    utils::write_file(path, xml.str());
}

} // namespace modules
} // namespace cstatic
