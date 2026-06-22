#pragma once

#include "config/config.hpp"
#include <string>
#include <map>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <inja/inja.hpp>
#include <regex>

namespace cstatic {

// Thrown when a template render fails. Carries context for structured reporting.
class RenderError : public std::runtime_error {
public:
    RenderError(const std::string& source_file, const std::string& template_name,
                int line, const std::string& message)
        : std::runtime_error(message),
          source_file_(source_file),
          template_name_(template_name),
          line_(line) {}

    const std::string& source_file() const { return source_file_; }
    const std::string& template_name() const { return template_name_; }
    int line() const { return line_; }

private:
    std::string source_file_;
    std::string template_name_;
    int line_;
};

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
    // source_file (if non-empty) is included in RenderError for better diagnostics.
    std::string render(const std::string& layout, const nlohmann::json& data,
                       const std::string& source_file = "") const;

    // Preload a template into the cache.
    void preload_template(const std::string& name) const;

    // Set the asset manifest for {{ asset() }} lookups.
    void set_asset_manifest(const std::map<std::string, std::string>& manifest);

    // Returns file paths of all ancestor templates (via {% extends %}).
    // Used for incremental build dependency tracking.
    std::vector<std::string> template_ancestors(const std::string& name) const;

private:
    std::string template_dir_;
    mutable inja::Environment env_;
    mutable std::unordered_map<std::string, std::string> template_cache_;
    mutable std::unordered_map<std::string, std::vector<std::string>> inheritance_deps_cache_;
    std::map<std::string, std::string> asset_manifest_;

    // Load a template file. Returns empty string if not found.
    std::string load_template(const std::string& name) const;

    // --- Template inheritance ({% extends %} / {% block %}) ---
    // Returns parent template name from {% extends "name" %}, or "" if none.
    std::string has_extends(const std::string& raw) const;
    // Recursively resolve extends chain, preserving block tags for unoverridden
    // blocks so deeper levels can still override them.
    std::string resolve_with_tags(const std::string& raw, int depth,
                                   const std::string& name,
                                   std::vector<std::string>& ancestors) const;
    // Extract all {% block %}s from a child template → map<name, content>.
    std::map<std::string, std::string> extract_blocks(const std::string& raw,
                                                       const std::string& name) const;
    // Walk a resolved parent, applying child overrides. When preserve_unoverridden
    // is true, unoverridden blocks are re-wrapped so deeper levels can override.
    std::string apply_blocks(const std::string& tmpl,
                             const std::map<std::string, std::string>& overrides,
                             bool preserve_unoverridden,
                             const std::string& name) const;
    // Remove all {% block %}/{% endblock %} tags, keeping default content.
    std::string strip_blocks(const std::string& tmpl,
                             const std::string& name) const;
};

} // namespace cstatic
