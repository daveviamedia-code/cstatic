#pragma once

#include <string>

namespace cstatic {

struct Config;

namespace modules {

// Generate robots.txt from config and write to output_dir.
void generate_robots(const Config& cfg, const std::string& output_dir);

} // namespace modules
} // namespace cstatic
