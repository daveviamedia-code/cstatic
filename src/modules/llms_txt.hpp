#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace cstatic {

struct Config;

namespace modules {

// Generate llms.txt and llms-full.txt (https://llmstxt.org) from the page
// list, writing them to <output_dir>/llms.txt and <output_dir>/llms-full.txt.
//
//   llms.txt      compact catalog; honors cfg.llms_txt_max_pages (0 = no cap)
//   llms-full.txt every non-excluded page, never capped
//
// Pages are listed in the order given (builder passes pages_array already
// sorted by date descending). Pages whose URL or title is empty, or whose URL
// matches any cfg.llms_txt_exclude glob, are omitted. Each entry takes the
// form `- [Title](<base_url><url>): excerpt` with the excerpt truncated to
// 160 chars; the `: excerpt` portion is omitted when the page has none.
// The header summary falls back to cfg.site_description when
// cfg.llms_txt_description is unset.
void generate_llms_txt(const Config& cfg, const nlohmann::json& pages,
                       const std::string& output_dir);

} // namespace modules
} // namespace cstatic
