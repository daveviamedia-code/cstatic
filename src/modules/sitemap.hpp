#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace cstatic {

struct Config;

namespace modules {

// Generate sitemap.xml from the page list and write to output_dir.
// Pages matching any sitemap_exclude glob pattern are omitted.
void generate_sitemap(const Config& cfg, const nlohmann::json& pages,
                      const std::string& output_dir);

} // namespace modules
} // namespace cstatic
