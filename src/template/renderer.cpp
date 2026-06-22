#include "template/renderer.hpp"
#include "utils/path.hpp"
#include "utils/terminal.hpp"

#include <inja/inja.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <algorithm>

namespace cstatic {

namespace fs = std::filesystem;

namespace {

// Matches {% block NAME %} (group 1 = name) or {% endblock %} (no group).
const std::regex& block_tag_re() {
    static const std::regex re(R"(\{\%\s*(?:block\s+(\w+)|endblock)\s*\%\})");
    return re;
}

// Find the {% endblock %} that closes the block opened just before `start`.
// Returns {close_start, after_close}, or {npos, npos} if unbalanced.
std::pair<size_t, size_t> find_matching_close(const std::string& tmpl, size_t start) {
    int depth = 1;
    size_t pos = start;
    while (pos < tmpl.size()) {
        std::smatch m;
        if (!std::regex_search(tmpl.cbegin() + static_cast<std::ptrdiff_t>(pos),
                               tmpl.cend(), m, block_tag_re())) {
            break;
        }
        size_t tag_pos = pos + static_cast<size_t>(m.position());
        size_t after = tag_pos + static_cast<size_t>(m[0].length());
        if (m[1].matched) {
            ++depth;
        } else {
            --depth;
            if (depth == 0) {
                return {tag_pos, after};
            }
        }
        pos = after;
    }
    return {std::string::npos, std::string::npos};
}

int line_of(const std::string& s, size_t pos) {
    if (pos > s.size()) pos = s.size();
    return static_cast<int>(
        std::count(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(pos), '\n')) + 1;
}

} // namespace

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
    if (!f.is_open()) {
        return ""; // caller falls back to built-in template
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    std::string raw = ss.str();

    // Resolve template inheritance ({% extends %} / {% block %}) before caching.
    std::vector<std::string> ancestors;
    std::string resolved = strip_blocks(
        resolve_with_tags(raw, 0, name, ancestors), name);

    template_cache_[name] = resolved;
    inheritance_deps_cache_[name] = ancestors;
    return resolved;
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
    try {
        load_template(name);
    } catch (const RenderError&) {
        // Inheritance errors will surface again at render time via render().
    }
}

std::vector<std::string> TemplateRenderer::template_ancestors(const std::string& name) const {
    try {
        load_template(name);
    } catch (const RenderError&) {
        return {}; // errors surface at render time
    }
    auto it = inheritance_deps_cache_.find(name);
    if (it != inheritance_deps_cache_.end()) {
        return it->second;
    }
    return {};
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

// --- Template inheritance implementation ---

std::string TemplateRenderer::has_extends(const std::string& raw) const {
    static const std::regex extends_re(R"(\{\%\s*extends\s*["']([^"']+)["']\s*\%\})");
    std::smatch m;
    if (std::regex_search(raw, m, extends_re)) {
        return m[1].str();
    }
    return "";
}

std::map<std::string, std::string>
TemplateRenderer::extract_blocks(const std::string& raw,
                                  const std::string& name) const {
    std::map<std::string, std::string> blocks;
    // Stack entries: (block_name, content_start_position)
    std::vector<std::pair<std::string, size_t>> stack;
    size_t pos = 0;

    while (pos < raw.size()) {
        std::smatch m;
        if (!std::regex_search(raw.cbegin() + static_cast<std::ptrdiff_t>(pos),
                               raw.cend(), m, block_tag_re())) {
            break;
        }
        size_t tag_pos = pos + static_cast<size_t>(m.position());
        size_t after = tag_pos + static_cast<size_t>(m[0].length());

        if (m[1].matched) {
            // {% block NAME %}
            stack.push_back({m[1].str(), after});
            pos = after;
        } else {
            // {% endblock %}
            if (stack.empty()) {
                throw RenderError("", name, line_of(raw, tag_pos),
                    "{% endblock %} without matching {% block %}");
            }
            auto [bname, content_start] = stack.back();
            stack.pop_back();
            if (blocks.count(bname)) {
                throw RenderError("", name, line_of(raw, tag_pos),
                    "duplicate block name '" + bname + "'");
            }
            blocks[bname] = raw.substr(content_start, tag_pos - content_start);
            pos = after;
        }
    }

    if (!stack.empty()) {
        throw RenderError("", name, line_of(raw, stack.back().second),
            "block '" + stack.back().first + "' has no matching {% endblock %}");
    }
    return blocks;
}

std::string TemplateRenderer::apply_blocks(const std::string& tmpl,
                                            const std::map<std::string, std::string>& overrides,
                                            bool preserve_unoverridden,
                                            const std::string& name) const {
    std::string result;
    size_t pos = 0;

    while (pos < tmpl.size()) {
        std::smatch m;
        if (!std::regex_search(tmpl.cbegin() + static_cast<std::ptrdiff_t>(pos),
                               tmpl.cend(), m, block_tag_re())) {
            result += tmpl.substr(pos);
            break;
        }
        size_t tag_pos = pos + static_cast<size_t>(m.position());
        size_t after_tag = tag_pos + static_cast<size_t>(m[0].length());

        if (m[1].matched) {
            // Top-level {% block NAME %}
            std::string bname = m[1].str();
            result += tmpl.substr(pos, tag_pos - pos);

            auto [close_pos, after_close] = find_matching_close(tmpl, after_tag);
            if (close_pos == std::string::npos) {
                throw RenderError("", name, line_of(tmpl, tag_pos),
                    "block '" + bname + "' has no matching {% endblock %}");
            }
            std::string default_content = tmpl.substr(after_tag, close_pos - after_tag);

            auto it = overrides.find(bname);
            if (it != overrides.end()) {
                result += it->second; // verbatim
            } else {
                std::string new_default = apply_blocks(default_content, overrides,
                                                        preserve_unoverridden, name);
                if (preserve_unoverridden) {
                    result += "{% block " + bname + " %}" + new_default + "{% endblock %}";
                } else {
                    result += new_default;
                }
            }
            pos = after_close;
        } else {
            // {% endblock %} at depth 0 — unmatched
            throw RenderError("", name, line_of(tmpl, tag_pos),
                "{% endblock %} without matching {% block %}");
        }
    }
    return result;
}

std::string TemplateRenderer::strip_blocks(const std::string& tmpl,
                                            const std::string& name) const {
    std::string result;
    size_t pos = 0;

    while (pos < tmpl.size()) {
        std::smatch m;
        if (!std::regex_search(tmpl.cbegin() + static_cast<std::ptrdiff_t>(pos),
                               tmpl.cend(), m, block_tag_re())) {
            result += tmpl.substr(pos);
            break;
        }
        size_t tag_pos = pos + static_cast<size_t>(m.position());
        size_t after_tag = tag_pos + static_cast<size_t>(m[0].length());

        if (m[1].matched) {
            std::string bname = m[1].str();
            result += tmpl.substr(pos, tag_pos - pos);

            auto [close_pos, after_close] = find_matching_close(tmpl, after_tag);
            if (close_pos == std::string::npos) {
                throw RenderError("", name, line_of(tmpl, tag_pos),
                    "block '" + bname + "' has no matching {% endblock %}");
            }
            std::string default_content = tmpl.substr(after_tag, close_pos - after_tag);
            result += strip_blocks(default_content, name);
            pos = after_close;
        } else {
            throw RenderError("", name, line_of(tmpl, tag_pos),
                "{% endblock %} without matching {% block %}");
        }
    }
    return result;
}

std::string TemplateRenderer::resolve_with_tags(const std::string& raw, int depth,
                                                 const std::string& name,
                                                 std::vector<std::string>& ancestors) const {
    if (depth > 16) {
        throw RenderError("", name, 0,
            "circular template inheritance detected (depth > 16)");
    }

    std::string parent_name = has_extends(raw);
    if (parent_name.empty()) {
        return raw;
    }

    // Extract child's blocks (non-block content is discarded — Jinja2 semantics).
    auto child_blocks = extract_blocks(raw, name);

    // Load parent raw content (not resolved — resolution happens recursively).
    std::string parent_path = utils::path_join(template_dir_, parent_name + ".html");
    if (!fs::exists(parent_path)) {
        throw RenderError("", name, 0,
            "extends '" + parent_name + "' but parent template not found");
    }
    ancestors.push_back(parent_path);

    std::ifstream f(parent_path);
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string parent_raw = ss.str();

    // Recursively resolve the parent's own extends chain.
    std::vector<std::string> parent_ancestors;
    std::string parent_resolved = resolve_with_tags(parent_raw, depth + 1,
                                                     parent_name, parent_ancestors);
    for (const auto& a : parent_ancestors) {
        ancestors.push_back(a);
    }

    return apply_blocks(parent_resolved, child_blocks, true, parent_name);
}

} // namespace cstatic
