#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace cstatic {

struct Config;

namespace modules {

// Generate sitemap-ai.xml — a curated sitemap for AI/LLM crawlers that
// filters out thin pages (taxonomy listings, paginated indexes, low
// word-count pages). Optionally embeds <image:image> entries for richer
// discovery. Written to output_dir/sitemap-ai.xml.
void generate_sitemap_ai(const Config& cfg, const nlohmann::json& pages,
                         const std::string& output_dir);

} // namespace modules
} // namespace cstatic
