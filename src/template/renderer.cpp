#include "template/renderer.hpp"
#include "utils/path.hpp"
#include "utils/terminal.hpp"

#include <inja/inja.hpp>
#include <fstream>
#include <sstream>

namespace cstatic {

TemplateRenderer::TemplateRenderer(const std::string& template_dir)
    : template_dir_(template_dir) {}

std::string TemplateRenderer::load_template(const std::string& name) const {
    // Check cache first
    auto it = template_cache_.find(name);
    if (it != template_cache_.end()) {
        return it->second;
    }

    // Try with .html extension
    std::string path = utils::path_join(template_dir_, name + ".html");
    std::ifstream f(path);
    if (f.is_open()) {
        std::ostringstream ss;
        ss << f.rdbuf();
        std::string tmpl = ss.str();
        template_cache_[name] = tmpl;
        return tmpl;
    }
    return ""; // caller falls back to built-in template
}

std::string TemplateRenderer::render(const std::string& layout,
                                      const nlohmann::json& data) const {
    std::string tmpl = load_template(layout);
    if (tmpl.empty()) {
        // Fall back to a minimal built-in template
        tmpl = "<!DOCTYPE html>\n<html lang=\"{{ site.language }}\">\n"
               "<head><meta charset=\"utf-8\"><title>{{ page.title }}</title></head>\n"
               "<body>\n{{ page.content }}\n</body>\n</html>";
    }

    try {
        return env_.render(tmpl, data);
    } catch (const inja::InjaError& err) {
        std::string template_path = utils::path_join(template_dir_, layout + ".html");
        throw std::runtime_error(
            std::string(utils::error_label()) + " template error in '" + layout +
            "' (resolved to " + template_path + "): " + std::string(err.what())
        );
    }
}

void TemplateRenderer::preload_template(const std::string& name) const {
    load_template(name);
}

} // namespace cstatic
