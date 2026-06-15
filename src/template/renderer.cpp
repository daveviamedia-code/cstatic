#include "template/renderer.hpp"
#include "utils/path.hpp"
#include "utils/terminal.hpp"

#include <inja/inja.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace cstatic {

namespace fs = std::filesystem;

TemplateRenderer::TemplateRenderer(const std::string& template_dir)
    : template_dir_(template_dir) {

    // Pre-load all partials from templates/partials/ for thread-safe includes.
    fs::path partials_dir = fs::path(template_dir) / "partials";
    if (fs::exists(partials_dir) && fs::is_directory(partials_dir)) {
        for (const auto& entry : fs::directory_iterator(partials_dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            if (ext != ".html") continue;

            std::string name = entry.path().stem().string();
            std::ifstream f(entry.path());
            if (!f.is_open()) continue;

            std::ostringstream ss;
            ss << f.rdbuf();
            std::string content = ss.str();

            try {
                auto tmpl = env_.parse(content);
                env_.include_template(name, tmpl);
                template_cache_[name] = content;
            } catch (const inja::InjaError&) {
                // Skip partials with parse errors — will surface at render time
            }
        }
    }

    // Prevent file I/O during multi-threaded rendering.
    env_.set_search_included_templates_in_files(false);

    // Fallback callback for includes not found in pre-loaded storage.
    std::string tdir = template_dir;
    env_.set_include_callback([tdir](const std::string& name, const std::string&) -> inja::Template {
        // Try partials/ first, then template root
        std::vector<std::string> paths = {
            (fs::path(tdir) / "partials" / (name + ".html")).string(),
            (fs::path(tdir) / (name + ".html")).string()
        };
        for (const auto& p : paths) {
            std::ifstream f(p);
            if (f.is_open()) {
                std::ostringstream ss;
                ss << f.rdbuf();
                return inja::Environment().parse(ss.str());
            }
        }
        throw inja::FileError("Could not find include template '" + name + "'");
    });
}

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
                                      const nlohmann::json& data,
                                      const std::string& source_file) const {
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
        int line = static_cast<int>(err.location.line);
        throw RenderError(source_file, layout, line, err.message);
    }
}

void TemplateRenderer::preload_template(const std::string& name) const {
    load_template(name);
}

void TemplateRenderer::set_asset_manifest(const std::map<std::string, std::string>& manifest) {
    asset_manifest_ = manifest;

    // Register the {{ asset("path") }} callback
    // Store a pointer to the manifest member for the lambda capture
    auto* manifest_ptr = &asset_manifest_;
    env_.add_callback("asset", 1, [manifest_ptr](inja::Arguments& args) {
        std::string path = args.at(0)->get<std::string>();
        auto it = manifest_ptr->find(path);
        if (it != manifest_ptr->end()) return it->second;
        return path; // fallback to original path
    });
}

} // namespace cstatic
