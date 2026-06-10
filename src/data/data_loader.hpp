#pragma once

#include <string>
#include <map>
#include <nlohmann/json.hpp>

namespace cstatic {

// Loads data files (JSON and YAML) from a directory into nlohmann::json objects.
// Files are keyed by their stem (e.g. "products.json" → "products").
class DataLoader {
public:
    explicit DataLoader(const std::string& data_dir);

    // Load all data files from the data directory.
    // Returns a JSON object keyed by filename stem.
    nlohmann::json load_all() const;

    // Load a single data file by filename.
    nlohmann::json load_file(const std::string& filename) const;

private:
    std::string data_dir_;

    // Parse a JSON file into nlohmann::json.
    static nlohmann::json parse_json(const std::string& contents, const std::string& path);

    // Parse a YAML file into nlohmann::json.
    static nlohmann::json parse_yaml(const std::string& contents, const std::string& path);
};

} // namespace cstatic
