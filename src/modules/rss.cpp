#include "modules/rss.hpp"
#include "config/config.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ctime>
#include <iomanip>

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
    xml << "    <title>" << xml_escape(title) << "</title>\n";
    xml << "    <link>" << xml_escape(link) << "</link>\n";
    xml << "    <description>" << xml_escape(description) << "</description>\n";
    xml << "    <language>" << xml_escape(cfg.site_language) << "</language>\n";

    // pages_array is already sorted by date descending
    int count = 0;
    for (const auto& page : pages) {
        if (count >= cfg.rss_item_count) break;

        std::string page_title = page.value("title", "");
        std::string page_url = page.value("url", "");
        std::string page_date = page.value("date", "");

        if (page_url.empty() || page_title.empty()) continue;

        xml << "    <item>\n";
        xml << "      <title>" << xml_escape(page_title) << "</title>\n";
        xml << "      <link>" << xml_escape(cfg.site_base_url + page_url) << "</link>\n";
        if (!page_date.empty()) {
            xml << "      <pubDate>" << iso_to_rfc822(page_date) << "</pubDate>\n";
        }
        xml << "      <guid>" << xml_escape(cfg.site_base_url + page_url) << "</guid>\n";
        xml << "    </item>\n";

        count++;
    }

    xml << "  </channel>\n";
    xml << "</rss>\n";

    std::string path = output_dir + "/feed.xml";
    write_file(path, xml.str());
}

} // namespace modules
} // namespace cstatic
