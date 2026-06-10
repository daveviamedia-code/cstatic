#include "modules/robots.hpp"
#include "config/config.hpp"

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

} // anonymous namespace

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
    write_file(path, txt.str());
}

} // namespace modules
} // namespace cstatic
