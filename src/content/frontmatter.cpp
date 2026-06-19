#include "content/frontmatter.hpp"

#include <yaml-cpp/yaml.h>
#include <stdexcept>
#include <sstream>

namespace cstatic {

// Convert a YAML node to nlohmann::json with type inference.
static nlohmann::json yaml_node_to_json(const YAML::Node& n) {
    switch (n.Type()) {
        case YAML::NodeType::Null:
            return nullptr;
        case YAML::NodeType::Scalar: {
            // Type-infer bool/int/float/string
            std::string val = n.as<std::string>();
            if (val == "true" || val == "True" || val == "TRUE") return true;
            if (val == "false" || val == "False" || val == "FALSE") return false;
            try {
                size_t pos;
                auto iv = std::stoll(val, &pos);
                if (pos == val.size()) return iv;
            } catch (...) {}
            try {
                size_t pos;
                auto dv = std::stod(val, &pos);
                if (pos == val.size()) return dv;
            } catch (...) {}
            return val;
        }
        case YAML::NodeType::Sequence: {
            nlohmann::json arr = nlohmann::json::array();
            for (size_t i = 0; i < n.size(); i++) {
                arr.push_back(yaml_node_to_json(n[i]));
            }
            return arr;
        }
        case YAML::NodeType::Map: {
            nlohmann::json obj = nlohmann::json::object();
            for (auto it = n.begin(); it != n.end(); ++it) {
                obj[it->first.as<std::string>()] = yaml_node_to_json(it->second);
            }
            return obj;
        }
        default:
            return nullptr;
    }
}

ParsedContent parse_frontmatter(const std::string& content, const std::string& filename) {
    ParsedContent result;

    // Frontmatter must start at byte 0 with "---\n"
    if (content.size() < 4 || content.substr(0, 4) != "---\n") {
        // No frontmatter — entire content is body
        result.body = content;
        return result;
    }

    // Find closing "---"
    auto close_pos = content.find("\n---\n", 4);
    if (close_pos == std::string::npos) {
        // Check if it closes at the very end of file (no trailing newline)
        bool ends_with_delim = (content.size() >= 7 &&
            content.substr(content.size() - 4) == "\n---");
        if (!ends_with_delim) {
            // No closing delimiter — treat entire content as body
            result.body = content;
            return result;
        }
        close_pos = content.size() - 4;
    }

    std::string yaml_str = content.substr(4, close_pos - 4);
    result.body = content.substr(close_pos + 5); // skip past "\n---\n"

    // Parse YAML
    YAML::Node node;
    try {
        node = YAML::Load(yaml_str);
    } catch (const YAML::Exception& err) {
        // mark.line is 0-based and relative to yaml_str, which starts one line
        // below the file's `---` header. +2 converts to a 1-based FILE line so
        // source-context rendering lines up with what the user sees.
        // mark.column is 0-based; +1 makes it 1-based (columns are unaffected by
        // the frontmatter offset).
        int line = err.mark.is_null() ? 0 : err.mark.line + 2;
        int col  = err.mark.is_null() ? 0 : err.mark.column + 1;
        throw FrontmatterError(filename, line, col, err.msg);
    }

    // Field extraction — BadConversion from .as<T>() throws YAML::Exception
    // derivatives too, so wrap the whole block to surface structured errors
    // instead of crashing the build.
    try {
        if (!node.IsMap()) {
            // Frontmatter is not a map — skip it
            result.body = content;
            return result;
        }

        // Extract known fields
        if (node["title"]) {
            result.frontmatter.title = node["title"].as<std::string>();
        }
        if (node["layout"]) {
            result.frontmatter.layout = node["layout"].as<std::string>();
        }
        if (node["permalink"]) {
            result.frontmatter.permalink = node["permalink"].as<std::string>();
        }
        if (node["date"]) {
            result.frontmatter.date = node["date"].as<std::string>();
        }
        if (node["draft"]) {
            result.frontmatter.draft = node["draft"].as<bool>();
        }
        if (node["description"]) {
            result.frontmatter.description = node["description"].as<std::string>();
        }
        if (node["image"]) {
            result.frontmatter.image = node["image"].as<std::string>();
        }
        if (node["canonical"]) {
            result.frontmatter.canonical = node["canonical"].as<std::string>();
        }
        if (node["sitemap_changefreq"]) {
            result.frontmatter.sitemap_changefreq = node["sitemap_changefreq"].as<std::string>();
        }
        if (node["sitemap_priority"]) {
            // Tolerate both numeric (0.8) and string ("0.8") YAML
            try {
                result.frontmatter.sitemap_priority = node["sitemap_priority"].as<std::string>();
            } catch (...) {
                try {
                    double dv = node["sitemap_priority"].as<double>();
                    std::ostringstream ss;
                    ss << dv;
                    result.frontmatter.sitemap_priority = ss.str();
                } catch (...) {
                    result.frontmatter.sitemap_priority.clear();
                }
            }
        }
        if (node["tags"] && node["tags"].IsSequence()) {
            for (const auto& tag : node["tags"]) {
                result.frontmatter.tags.push_back(tag.as<std::string>());
            }
        }
        if (node["aliases"] && node["aliases"].IsSequence()) {
            for (const auto& a : node["aliases"]) {
                result.frontmatter.aliases.push_back(a.as<std::string>());
            }
        }

        // Collect remaining keys as custom fields
        static const char* known_keys[] = {
            "title", "layout", "permalink", "date", "tags", "aliases", "draft",
            "description", "image", "canonical", "sitemap_changefreq", "sitemap_priority"
        };
        for (auto it = node.begin(); it != node.end(); ++it) {
            std::string key = it->first.as<std::string>();
            bool is_known = false;
            for (auto k : known_keys) {
                if (key == k) { is_known = true; break; }
            }
            if (!is_known) {
                result.frontmatter.custom[key] = yaml_node_to_json(it->second);
            }
        }
    } catch (const FrontmatterError&) {
        // Already structured — let it propagate unchanged.
        throw;
    } catch (const YAML::Exception& err) {
        int line = err.mark.is_null() ? 0 : err.mark.line + 2;
        int col  = err.mark.is_null() ? 0 : err.mark.column + 1;
        throw FrontmatterError(filename, line, col, err.msg);
    }

    return result;
}

} // namespace cstatic
