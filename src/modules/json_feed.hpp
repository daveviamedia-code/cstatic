#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace cstatic {

struct Config;

namespace modules {

// Generate a JSON Feed 1.1 file (https://jsonfeed.org/version/1.1) from the
// page list and write it to <output_dir>/<cfg.json_feed_output>.
// Reuses cfg.rss_item_count to limit entries (most recent first) and
// cfg.rss_title / cfg.rss_description for feed-level metadata. Each page's
// rendered HTML body is emitted as the item's content_html.
void generate_json_feed(const Config& cfg, const nlohmann::json& pages,
                        const std::string& output_dir);

} // namespace modules
} // namespace cstatic
