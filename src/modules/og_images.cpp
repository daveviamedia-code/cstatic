#include "modules/og_images.hpp"
#include "config/config.hpp"
#include "utils/file_io.hpp"
#include "utils/path.hpp"
#include "utils/terminal.hpp"

#include <inja/inja.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <cctype>

namespace cstatic {
namespace modules {

namespace fs = std::filesystem;

namespace {

// Check whether a command-line tool is available on PATH.
bool tool_available(const std::string& tool) {
#ifdef _WIN32
    std::string cmd = "where " + tool + " >nul 2>nul";
#else
    std::string cmd = "which " + tool + " >/dev/null 2>&1";
#endif
    return std::system(cmd.c_str()) == 0;
}

// One-time SVG->PNG converter detection cache.
struct ConverterCache {
    bool checked = false;
    bool available = false;
    std::string tool;     // "rsvg-convert" | "convert" | "inkscape"
    bool warned = false;
};

ConverterCache& get_converter_cache() {
    static ConverterCache cache;
    return cache;
}

// Detect an available SVG->PNG converter, in priority order.
const ConverterCache& detect_converter() {
    auto& c = get_converter_cache();
    if (c.checked) return c;
    c.checked = true;
    const char* candidates[] = { "rsvg-convert", "convert", "inkscape" };
    for (const char* name : candidates) {
        if (tool_available(name)) {
            c.available = true;
            c.tool = name;
            break;
        }
    }
    return c;
}

// The image extension this build will produce for OG images ("png" or "svg").
// Emits a one-time notice when PNG is requested but no converter is available.
std::string og_extension(const Config& cfg) {
    if (cfg.og_images_output_format != "png") return "svg";
    const ConverterCache& conv = detect_converter();
    if (conv.available) return "png";
    auto& mut = get_converter_cache();
    if (!mut.warned) {
        std::cerr << utils::notice_label() << " no SVG->PNG converter found "
                  << "(rsvg-convert / convert / inkscape) "
                  << "— writing SVG OG images instead\n";
        mut.warned = true;
    }
    return "svg";
}

// Run the selected converter to render an SVG file to PNG.
bool run_svg_converter(const std::string& tool, const std::string& svg_path,
                       const std::string& png_path, int width, int height) {
    std::ostringstream cmd;
    if (tool == "rsvg-convert") {
        cmd << "rsvg-convert -w " << width << " -h " << height
            << " \"" << svg_path << "\" -o \"" << png_path << "\"";
    } else if (tool == "convert") {
        cmd << "convert -density 150 \"" << svg_path
            << "\" -resize " << width << "x" << height
            << " \"" << png_path << "\"";
    } else if (tool == "inkscape") {
        cmd << "inkscape --export-type-png --export-filename=\""
            << png_path << "\" \"" << svg_path << "\"";
    } else {
        return false;
    }
#ifndef _WIN32
    cmd << " >/dev/null 2>&1";
#endif
    return std::system(cmd.str().c_str()) == 0;
}

// Derive a filesystem- and shell-safe slug from a page URL.
//   "/posts/hello/" -> "posts-hello"
//   "/"             -> "index"
std::string slug_from_url(const std::string& url) {
    std::string s = url;
    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    while (!s.empty() && s.back() == '/') s.pop_back();

    std::string out = s.empty() ? std::string("index") : s;

    // Collapse to a safe character set (alphanumeric, '-', '_', '.').
    std::string safe;
    safe.reserve(out.size());
    for (char c : out) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
            safe += c;
        } else {
            safe += '-';
        }
    }
    return safe;
}

// Build a root-relative URL for an OG image filename under the configured dir.
std::string og_url(const std::string& subdir, const std::string& filename) {
    std::string d = subdir;
    while (!d.empty() && d.back() == '/') d.pop_back();
    while (!d.empty() && d.front() == '/') d.erase(d.begin());
    return "/" + d + "/" + filename;
}

} // anonymous namespace

std::string og_image_url_for(const Config& cfg, const std::string& page_url) {
    std::string slug = slug_from_url(page_url);
    std::string ext = og_extension(cfg);
    return og_url(cfg.og_images_output_dir, slug + "." + ext);
}

int generate_og_images(const Config& cfg, nlohmann::json& pages,
                       const std::string& output_dir,
                       const std::string& template_dir) {
    // Load the SVG template (Inja-rendered).
    std::string tmpl_path = utils::path_join(template_dir, cfg.og_images_template + ".svg");
    std::string tmpl_content = utils::read_file_or_empty(tmpl_path);
    if (tmpl_content.empty()) {
        std::cerr << utils::warning_label() << " OG image template not found: "
                  << tmpl_path << " — skipping OG image generation\n";
        return 0;
    }

    // Prepare output directory.
    std::string og_out_dir = utils::path_join(output_dir, cfg.og_images_output_dir);
    fs::create_directories(og_out_dir);

    std::string ext = og_extension(cfg);
    bool produce_png = (ext == "png");
    std::string detected_tool = produce_png ? detect_converter().tool : "";

    // Standalone Inja environment (avoids clashing with the renderer's template dir).
    inja::Environment env;

    inja::Template tmpl;
    try {
        tmpl = env.parse(tmpl_content);
    } catch (const inja::InjaError& e) {
        std::cerr << utils::warning_label() << " OG image template parse error: "
                  << e.message << "\n";
        return 0;
    }

    int count = 0;
    for (auto& page : pages) {
        std::string title = page.value("title", "");
        if (title.empty()) continue;

        std::string url = page.value("url", "");
        std::string slug = slug_from_url(url);

        // Build Inja context. XML-escape text values so the generated SVG stays valid.
        nlohmann::json data;
        data["page"]["title"]   = utils::xml_escape(title);
        data["page"]["date"]    = utils::xml_escape(page.value("date", ""));
        data["page"]["excerpt"] = utils::xml_escape(page.value("excerpt", ""));
        data["page"]["url"]     = url;
        data["site"]["title"]   = utils::xml_escape(cfg.site_title);
        data["site"]["base_url"] = cfg.site_base_url;

        std::string svg;
        try {
            svg = env.render(tmpl, data);
        } catch (const inja::InjaError& e) {
            std::cerr << utils::warning_label() << " OG image template error for '"
                      << title << "': " << e.message << "\n";
            continue;
        }

        // Write the SVG (always — useful as a fallback and for the "svg" format).
        std::string svg_path = utils::path_join(og_out_dir, slug + ".svg");
        utils::write_file(svg_path, svg);

        if (produce_png) {
            std::string png_path = utils::path_join(og_out_dir, slug + ".png");
            run_svg_converter(detected_tool, svg_path, png_path,
                              cfg.og_images_width, cfg.og_images_height);
        }

        page["og_image"] = og_url(cfg.og_images_output_dir, slug + "." + ext);
        count++;
    }

    return count;
}

} // namespace modules
} // namespace cstatic
