#include "content/frontmatter.hpp"
#include "utils/terminal.hpp"

#include <yaml-cpp/yaml.h>
#include <stdexcept>
#include <sstream>

namespace cstatic {

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
    } catch (const YAML::ParserException& err) {
        std::ostringstream msg;
        msg << utils::error_label() << " " << filename
            << ": invalid YAML frontmatter (line " << err.mark.line + 1
            << "): " << err.msg;
        throw std::runtime_error(msg.str());
    }

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
    if (node["tags"] && node["tags"].IsSequence()) {
        for (const auto& tag : node["tags"]) {
            result.frontmatter.tags.push_back(tag.as<std::string>());
        }
    }

    // Collect remaining keys as custom fields
    static const char* known_keys[] = {
        "title", "layout", "permalink", "date", "tags", "draft"
    };
    for (auto it = node.begin(); it != node.end(); ++it) {
        std::string key = it->first.as<std::string>();
        bool is_known = false;
        for (auto k : known_keys) {
            if (key == k) { is_known = true; break; }
        }
        if (!is_known && it->second.IsScalar()) {
            result.frontmatter.custom[key] = it->second.as<std::string>();
        }
    }

    return result;
}

} // namespace cstatic
