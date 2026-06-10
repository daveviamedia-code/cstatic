#include "data/data_loader.hpp"
#include "utils/terminal.hpp"

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace cstatic {

namespace fs = std::filesystem;

DataLoader::DataLoader(const std::string& data_dir)
    : data_dir_(data_dir) {}

nlohmann::json DataLoader::load_all() const {
    nlohmann::json result = nlohmann::json::object();

    if (!fs::exists(data_dir_)) {
        return result; // no data directory is fine — return empty
    }

    for (const auto& entry : fs::recursive_directory_iterator(data_dir_)) {
        if (!entry.is_regular_file()) continue;

        std::string path = entry.path().string();
        std::string ext = entry.path().extension().string();

        // Only process JSON and YAML files
        if (ext != ".json" && ext != ".yaml" && ext != ".yml") continue;

        // Key is the relative path from data_dir, without extension
        std::string rel = path.substr(data_dir_.size());
        while (!rel.empty() && rel.front() == '/') rel = rel.substr(1);
        // Remove extension
        if (rel.size() > ext.size()) {
            rel = rel.substr(0, rel.size() - ext.size());
        }

        result[rel] = load_file(path);
    }

    return result;
}

nlohmann::json DataLoader::load_file(const std::string& filename) const {
    std::ifstream f(filename);
    if (!f.is_open()) {
        throw std::runtime_error(
            std::string(utils::error_label()) + " cannot read data file: " + filename +
            " — file does not exist or is unreadable"
        );
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string contents = ss.str();

    std::string ext = fs::path(filename).extension().string();
    if (ext == ".json") {
        return parse_json(contents, filename);
    } else if (ext == ".yaml" || ext == ".yml") {
        return parse_yaml(contents, filename);
    }

    throw std::runtime_error(
        std::string(utils::error_label()) + " unsupported data file format: " + filename +
        " — only .json, .yaml, and .yml are supported"
    );
}

nlohmann::json DataLoader::parse_json(const std::string& contents, const std::string& path) {
    try {
        return nlohmann::json::parse(contents);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(
            std::string(utils::error_label()) + " " + path + ": invalid JSON — " + e.what()
        );
    }
}

nlohmann::json DataLoader::parse_yaml(const std::string& contents, const std::string& path) {
    try {
        YAML::Node node = YAML::Load(contents);

        // Convert YAML node to JSON recursively
        std::function<nlohmann::json(const YAML::Node&)> convert;
        convert = [&convert](const YAML::Node& n) -> nlohmann::json {
            switch (n.Type()) {
                case YAML::NodeType::Null:
                    return nullptr;
                case YAML::NodeType::Scalar: {
                    // Try bool first
                    if (n.Tag() == "?" || n.Tag() == "!!") {
                        std::string val = n.as<std::string>();
                        if (val == "true" || val == "True" || val == "TRUE") return true;
                        if (val == "false" || val == "False" || val == "FALSE") return false;
                        // Try integer
                        try {
                            size_t pos;
                            auto iv = std::stoll(val, &pos);
                            if (pos == val.size()) return iv;
                        } catch (...) {}
                        // Try float
                        try {
                            size_t pos;
                            auto dv = std::stod(val, &pos);
                            if (pos == val.size()) return dv;
                        } catch (...) {}
                        return val;
                    }
                    return n.as<std::string>();
                }
                case YAML::NodeType::Sequence: {
                    nlohmann::json arr = nlohmann::json::array();
                    for (size_t i = 0; i < n.size(); i++) {
                        arr.push_back(convert(n[i]));
                    }
                    return arr;
                }
                case YAML::NodeType::Map: {
                    nlohmann::json obj = nlohmann::json::object();
                    for (auto it = n.begin(); it != n.end(); ++it) {
                        obj[it->first.as<std::string>()] = convert(it->second);
                    }
                    return obj;
                }
                default:
                    return nullptr;
            }
        };

        return convert(node);
    } catch (const YAML::Exception& e) {
        throw std::runtime_error(
            std::string(utils::error_label()) + " " + path + ": invalid YAML — " + e.what()
        );
    }
}

} // namespace cstatic
