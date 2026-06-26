#include "modules/robots.hpp"
#include "config/config.hpp"
#include "utils/file_io.hpp"

#include <sstream>
#include <vector>

namespace cstatic {
namespace modules {

namespace {

// Known AI/LLM web crawlers. Used when modules.robots_ai_crawlers_mode is set
// to "allow" or "disallow". Wrapped in a function to avoid static-init issues.
const std::vector<std::string>& ai_agents() {
    static const std::vector<std::string> agents = {
        "GPTBot",            // OpenAI (ChatGPT)
        "OAI-SearchBot",     // OpenAI web search
        "ClaudeBot",         // Anthropic (Claude)
        "PerplexityBot",     // Perplexity crawler
        "Perplexity-User",   // Perplexity live preview
        "CCBot",             // Common Crawl (feeds many LLMs)
        "Google-Extended",   // Google AI / Gemini training & inference
        "Applebot-Extended", // Apple AI training
        "Meta-ExternalAgent",// Meta AI
        "Amazonbot",         // Amazon (Alexa / AI)
        "Bytespider",        // ByteDance
        "Diffbot"            // Diffbot
    };
    return agents;
}

// Emit per-agent blocks for AI crawlers. Each block is separated by a blank
// line from preceding content. No-op when mode == "off".
void emit_ai_crawler_blocks(std::ostringstream& txt, const Config& cfg) {
    const std::string& mode = cfg.robots_ai_crawlers_mode;
    if (mode == "off" || mode.empty()) return;

    std::string directive;
    std::vector<std::string> agents;
    if (mode == "allow") {
        directive = "Allow";
        agents = ai_agents();
    } else if (mode == "disallow") {
        directive = "Disallow";
        agents = ai_agents();
    } else if (mode == "custom") {
        directive = "Allow";
        agents = cfg.robots_ai_crawlers_custom;
    } else {
        return; // config.cpp validates, but be defensive
    }

    for (const auto& agent : agents) {
        if (agent.empty()) continue;
        txt << "\nUser-agent: " << agent << "\n";
        txt << directive << ": /\n";
    }
}

} // anonymous namespace

void generate_robots(const Config& cfg, const std::string& output_dir) {
    std::ostringstream txt;

    // Main user-agent block (applies to all crawlers).
    txt << "User-agent: " << cfg.robots_user_agent << "\n";
    for (const auto& path : cfg.robots_disallow) {
        txt << "Disallow: " << path << "\n";
    }

    // Opt-in AI crawler blocks (allow/disallow/custom). Default "off" emits
    // nothing, preserving the classic single-block robots.txt.
    emit_ai_crawler_blocks(txt, cfg);

    // Sitemap directive (standalone; conventionally placed last).
    if (cfg.robots_include_sitemap && cfg.module_sitemap) {
        txt << "\nSitemap: " << cfg.site_base_url << "/sitemap.xml\n";
    }

    txt << "\n";

    std::string path = output_dir + "/robots.txt";
    utils::write_file(path, txt.str());
}

} // namespace modules
} // namespace cstatic
