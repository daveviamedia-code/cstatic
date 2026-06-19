#include "modules/json_feed.hpp"
#include "config/config.hpp"
#include "utils/file_io.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>

namespace cstatic {
namespace modules {

void generate_json_feed(const Config& cfg, const nlohmann::json& pages,
                        const std::string& output_dir) {
    const std::string title = cfg.rss_title.empty() ? cfg.site_title : cfg.rss_title;
    const std::string base_url = cfg.site_base_url;
    const std::string feed_filename = cfg.json_feed_output.empty()
        ? std::string("feed.json") : cfg.json_feed_output;

    nlohmann::json feed;
    feed["version"]       = "https://jsonfeed.org/version/1.1";
    feed["title"]         = title;
    feed["home_page_url"] = base_url;
    feed["feed_url"]      = base_url.empty()
        ? feed_filename
        : (base_url.back() == '/' ? base_url + feed_filename
                                  : base_url + "/" + feed_filename);
    if (!cfg.rss_description.empty()) {
        feed["description"] = cfg.rss_description;
    }
    if (!cfg.site_language.empty()) {
        feed["language"] = cfg.site_language;
    }

    // pages_array is already sorted by date descending (builder.cpp Phase 1).
    nlohmann::json items = nlohmann::json::array();
    int count = 0;
    for (const auto& page : pages) {
        if (count >= cfg.rss_item_count) break;

        std::string page_title   = page.value("title", "");
        std::string page_url     = page.value("url", "");
        std::string page_date    = page.value("date", "");
        std::string page_excerpt = page.value("excerpt", "");
        std::string page_html    = page.value("content_html", "");

        if (page_url.empty() || page_title.empty()) continue;

        std::string full_url = base_url + page_url;

        nlohmann::json item;
        item["id"]    = full_url;
        item["url"]   = full_url;
        item["title"] = page_title;
        if (!page_html.empty()) {
            item["content_html"] = page_html;
        } else if (!page_excerpt.empty()) {
            // Spec: an item should carry content_html and/or content_text.
            // Fall back to the plain-text excerpt when no HTML body is on
            // record (e.g. data-driven pages without rendered markdown).
            item["content_text"] = page_excerpt;
        }
        if (!page_excerpt.empty()) {
            item["summary"] = page_excerpt;
        }
        if (!page_date.empty()) {
            // Page dates are stored as YYYY-MM-DD which is valid ISO 8601.
            item["date_published"] = page_date;
        }
        items.push_back(item);
        count++;
    }
    feed["items"] = items;

    std::string path = output_dir + "/" + feed_filename;
    utils::write_file(path, feed.dump(2));
}

} // namespace modules
} // namespace cstatic
