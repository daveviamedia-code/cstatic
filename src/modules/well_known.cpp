#include "modules/well_known.hpp"
#include "config/config.hpp"
#include "utils/file_io.hpp"
#include "utils/slugify.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>

namespace cstatic {
namespace modules {

namespace fs = std::filesystem;

namespace {

// Resolve a possibly-relative path/URL against the site base URL. Absolute
// URLs (containing "://") are returned unchanged; relative paths are joined
// to the base URL with exactly one separating slash.
std::string resolve_url(const std::string& base_url, const std::string& p) {
    if (p.find("://") != std::string::npos) return p;
    if (p.empty()) return base_url;
    std::string prefix = (p.front() == '/') ? "" : "/";
    return base_url + prefix + p;
}

// Build the OpenAI plugin manifest object from config + site defaults.
nlohmann::json build_ai_plugin(const Config& cfg) {
    nlohmann::json m;
    m["schema_version"] = cfg.wk_ai_plugin_schema_version;

    const std::string& name = !cfg.wk_ai_plugin_name.empty()
        ? cfg.wk_ai_plugin_name : cfg.site_title;
    m["name_for_human"] = name;
    m["name_for_model"] = utils::slugify(name);

    const std::string& desc = !cfg.wk_ai_plugin_description.empty()
        ? cfg.wk_ai_plugin_description : cfg.site_description;
    if (!desc.empty()) {
        m["description_for_human"] = desc;
        m["description_for_model"] = desc;
    }

    // Static sites have no authenticated API surface; advertise no auth and
    // point the OpenAPI spec at the conventional site-relative URL (the author
    // drops a static/openapi.json if they want a live API contract).
    m["auth"] = nlohmann::json::object({{"type", "none"}});
    m["api"] = nlohmann::json::object({
        {"type", "openapi"},
        {"url",  resolve_url(cfg.site_base_url, "/openapi.json")}
    });

    if (!cfg.org_logo.empty()) {
        m["logo_url"] = resolve_url(cfg.site_base_url, cfg.org_logo);
    }

    return m;
}

} // anonymous namespace

void generate_well_known(const Config& cfg, const std::string& output_dir) {
    if (!cfg.wk_ai_plugin_enabled && !cfg.wk_security_txt_enabled) return;

    const std::string dir = output_dir + "/.well-known";
    std::error_code ec;
    fs::create_directories(dir, ec);

    if (cfg.wk_ai_plugin_enabled) {
        utils::write_file(dir + "/ai-plugin.json", build_ai_plugin(cfg).dump(2));
    }
    if (cfg.wk_security_txt_enabled) {
        utils::write_file(dir + "/security.txt", cfg.wk_security_txt_content);
    }
}

} // namespace modules
} // namespace cstatic
