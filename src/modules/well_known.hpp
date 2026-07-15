#pragma once

#include <string>

namespace cstatic {

struct Config;

namespace modules {

// Generate .well-known/ discovery files under <output_dir>/.well-known/:
//   ai-plugin.json   OpenAI plugin manifest (when cfg.wk_ai_plugin_enabled)
//   security.txt     raw security policy (when cfg.wk_security_txt_enabled)
//
// Both are opt-in. When both are disabled the call is a no-op and the
// .well-known/ directory is not created. ai-plugin.json defaults
// name/description from site_title/site_description and resolves org_logo
// (if set) against the site base URL. security.txt content is written
// verbatim from cfg.wk_security_txt_content.
void generate_well_known(const Config& cfg, const std::string& output_dir);

} // namespace modules
} // namespace cstatic
