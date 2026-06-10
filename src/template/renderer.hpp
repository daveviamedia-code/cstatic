#pragma once

#include "config/config.hpp"
#include <string>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>

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

private:
    std::string template_dir_;

    // Load a template file. Returns empty string if not found.
    std::string load_template(const std::string& name) const;
};

} // namespace cstatic
