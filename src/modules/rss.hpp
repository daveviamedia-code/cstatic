#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace cstatic {

struct Config;

namespace modules {

// Generate RSS 2.0 feed.xml from the page list and write to output_dir.
// Uses cfg.rss_item_count to limit entries (most recent first).
void generate_rss(const Config& cfg, const nlohmann::json& pages,
                  const std::string& output_dir);

} // namespace modules
} // namespace cstatic
