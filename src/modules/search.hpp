#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace cstatic {

struct Config;

namespace modules {

// Generate a client-side search index (JSON) from the page list and write to output_dir.
// Output filename is controlled by cfg.search_output.
void generate_search_index(const Config& cfg, const nlohmann::json& pages,
                           const std::string& output_dir);

} // namespace modules
} // namespace cstatic
