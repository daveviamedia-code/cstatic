#include "modules/robots.hpp"
#include "config/config.hpp"
#include "utils/file_io.hpp"

#include <sstream>

namespace cstatic {
namespace modules {

void generate_robots(const Config& cfg, const std::string& output_dir) {
    std::ostringstream txt;

    txt << "User-agent: " << cfg.robots_user_agent << "\n";

    for (const auto& path : cfg.robots_disallow) {
        txt << "Disallow: " << path << "\n";
    }

    if (cfg.robots_include_sitemap && cfg.module_sitemap) {
        txt << "\nSitemap: " << cfg.site_base_url << "/sitemap.xml\n";
    }

    txt << "\n";

    std::string path = output_dir + "/robots.txt";
    utils::write_file(path, txt.str());
}

} // namespace modules
} // namespace cstatic
