#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace cstatic {

struct Config;

namespace modules {

// Compute the deterministic og:image URL for a page (e.g. "/og/posts-hello.png")
// without writing any file. Used to populate page metadata before rendering so the
// {{ seo_meta }} og:image tag can reference it.
std::string og_image_url_for(const Config& cfg, const std::string& page_url);

// Generate Open Graph images for every page in `pages` that has a non-empty title.
// Renders an Inja SVG template (templates/<template_name>.svg) per page, writes the
// SVG to output/<output_dir>/<slug>.svg, and (when a converter is available and the
// requested format is "png") a PNG alongside it.
//
// Each generated page gains an `og_image` field holding the root-relative image URL
// (e.g. "/og/posts-hello.png"). This propagates into sitemap.xml, RSS, and — when
// called before rendering — the {{ seo_meta }} og:image tag.
//
// Returns the number of images generated.
int generate_og_images(const Config& cfg, nlohmann::json& pages,
                       const std::string& output_dir,
                       const std::string& template_dir);

} // namespace modules
} // namespace cstatic
