#include "modules/sitemap_ai.hpp"
#include "config/config.hpp"
#include "utils/file_io.hpp"
#include "utils/glob.hpp"

#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace cstatic {
namespace modules {

namespace {

// Inherit regular sitemap exclusions + drop thin taxonomy/paginated URLs.
bool is_excluded(const std::string& url, const std::vector<std::string>& patterns) {
    return utils::matches_any_glob(url, patterns);
}

bool is_thin_url(const std::string& url) {
    return url.find("/tags/") != std::string::npos ||
           url.find("/categories/") != std::string::npos ||
           url.find("/page/") != std::string::npos;
}

// Prepend base_url unless the image URL is already absolute (has ://).
std::string resolve_image_url(const std::string& base, const std::string& img) {
    if (img.find("://") != std::string::npos) return img;
    return base + img;
}

// Collect deduped image URLs from og_image + image fields.
std::vector<std::string> collect_images(const nlohmann::json& page, const std::string& base) {
    std::vector<std::string> images;
    std::vector<std::string> keys = {"og_image", "image"};
    for (const auto& key : keys) {
        if (!page.contains(key)) continue;
        std::string img = page.value(key, "");
        if (img.empty()) continue;
        std::string resolved = resolve_image_url(base, img);
        // dedupe
        bool dup = false;
        for (const auto& existing : images) {
            if (existing == resolved) { dup = true; break; }
        }
        if (!dup) images.push_back(resolved);
    }
    return images;
}

bool type_excluded(const nlohmann::json& page,
                   const std::vector<std::string>& exclude_types) {
    if (exclude_types.empty()) return false;
    if (!page.contains("type") || !page["type"].is_string()) return false;
    const std::string& t = page["type"].get<std::string>();
    for (const auto& ex : exclude_types) {
        if (ex == t) return true;
    }
    return false;
}

} // anonymous namespace

void generate_sitemap_ai(const Config& cfg, const nlohmann::json& pages,
                         const std::string& output_dir) {
    // First pass: determine whether any included page has images, so we know
    // whether to declare the image namespace on <urlset>.
    bool any_images = false;
    if (cfg.sitemap_ai_include_images) {
        for (const auto& page : pages) {
            std::string url = page.value("url", "");
            if (url.empty()) continue;
            if (is_excluded(url, cfg.sitemap_exclude)) continue;
            if (is_thin_url(url)) continue;
            int wc = page.value("word_count", 0);
            if (wc <= 100) continue;
            if (type_excluded(page, cfg.sitemap_ai_exclude_types)) continue;
            if (!collect_images(page, cfg.site_base_url).empty()) {
                any_images = true;
                break;
            }
        }
    }

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\"";
    if (any_images) {
        xml << " xmlns:image=\"http://www.google.com/schemas/sitemap-image/1.1\"";
    }
    xml << ">\n";

    for (const auto& page : pages) {
        std::string url = page.value("url", "");
        if (url.empty()) continue;
        if (is_excluded(url, cfg.sitemap_exclude)) continue;
        if (is_thin_url(url)) continue;
        int wc = page.value("word_count", 0);
        if (wc <= 100) continue;
        if (type_excluded(page, cfg.sitemap_ai_exclude_types)) continue;

        xml << "  <url>\n";
        xml << "    <loc>" << utils::xml_escape(cfg.site_base_url + url) << "</loc>\n";

        std::string date = page.value("date", "");
        if (!date.empty()) {
            xml << "    <lastmod>" << utils::xml_escape(date) << "</lastmod>\n";
        }

        if (cfg.sitemap_ai_include_images) {
            auto images = collect_images(page, cfg.site_base_url);
            for (const auto& img : images) {
                xml << "    <image:image><image:loc>"
                    << utils::xml_escape(img)
                    << "</image:loc></image:image>\n";
            }
        }

        xml << "  </url>\n";
    }

    xml << "</urlset>\n";

    std::string path = output_dir + "/sitemap-ai.xml";
    utils::write_file(path, xml.str());
}

} // namespace modules
} // namespace cstatic
