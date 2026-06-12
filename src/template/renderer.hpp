#pragma once

#include "config/config.hpp"
#include <string>
#include <map>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <inja/inja.hpp>

namespace cstatic {

// A fully-resolved page ready for output.
struct Page {
    std::string url;                  // e.g. "/posts/hello/"
    std::string output_path;          // e.g. "output/posts/hello/index.html"
    std::string title;
    std::string date;
    std::string layout = "default";
    bool draft = false;
    nlohmann::json template_data;     // all data passed to the template
};

// Load and cache templates from the template directory.
class TemplateRenderer {
public:
    explicit TemplateRenderer(const std::string& template_dir);

    // Render a page using its layout template.
    // The template_data JSON object is passed as the inja context.
    std::string render(const std::string& layout, const nlohmann::json& data) const;

    // Preload a template into the cache.
    void preload_template(const std::string& name) const;

private:
    std::string template_dir_;
    mutable inja::Environment env_;
    mutable std::unordered_map<std::string, std::string> template_cache_;

    // Load a template file. Returns empty string if not found.
    std::string load_template(const std::string& name) const;
};

} // namespace cstatic
