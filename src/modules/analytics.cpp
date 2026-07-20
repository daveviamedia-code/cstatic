#include "modules/analytics.hpp"
#include "config/config.hpp"
#include "utils/terminal.hpp"

#include <iostream>
#include <string>

namespace cstatic {
namespace modules {

namespace {

// AI referrer hostname patterns. Matched as a substring against the lowercased
// hostname of document.referrer (e.g. "copilot.microsoft.com" matches both
// "copilot.microsoft.com" and any subdomain thereof).
constexpr const char* kAiReferrerDomains[] = {
    "perplexity.ai",
    "chatgpt.com",
    "copilot.microsoft.com",
    "gemini.google.com",
    "claude.ai",
    "you.com",
    "poe.com",
    "phind.com",
    "kagi.com"
};

// AI name prefixes used to scan URL params (utm_source, source, ref, from).
// Matched as a case-insensitive prefix of the param value.
constexpr const char* kAiSourcePrefixes[] = {
    "perplexity",
    "chatgpt",
    "copilot",
    "gemini",
    "claude",
    "you.com",
    "poe",
    "phind",
    "kagi"
};

constexpr const char* kUrlParamKeys[] = {
    "utm_source",
    "source",
    "ref",
    "from"
};

// Emit a JS array literal of double-quoted strings.
template <size_t N>
std::string js_string_array(const char* const (&arr)[N]) {
    std::string s = "[";
    for (size_t i = 0; i < N; ++i) {
        if (i) s += ", ";
        s += "\"";
        s += arr[i];
        s += "\"";
    }
    s += "]";
    return s;
}

// Emit the JS body (inside <script>...</script>). Provider is already
// validated by the caller; endpoint is required only for "custom".
std::string build_script_body(const std::string& provider,
                              const std::string& endpoint) {
    std::string s;
    s += "(function(){\n";
    s += "  var aiDomains = " + js_string_array(kAiReferrerDomains) + ";\n";
    s += "  var aiPrefixes = " + js_string_array(kAiSourcePrefixes) + ";\n";
    s += "  function detectSource() {\n";
    s += "    try {\n";
    s += "      var ref = document.referrer || \"\";\n";
    s += "      if (ref) {\n";
    s += "        var host = ref.replace(/^[a-z]+:\\/\\//i, \"\").split(\"/\")[0].toLowerCase();\n";
    s += "        for (var i = 0; i < aiDomains.length; i++) {\n";
    s += "          if (host.indexOf(aiDomains[i]) !== -1) return aiDomains[i];\n";
    s += "        }\n";
    s += "      }\n";
    s += "      var params = new URLSearchParams(window.location.search);\n";
    s += "      var keys = " + js_string_array(kUrlParamKeys) + ";\n";
    s += "      for (var k = 0; k < keys.length; k++) {\n";
    s += "        var v = (params.get(keys[k]) || \"\").toLowerCase();\n";
    s += "        if (!v) continue;\n";
    s += "        for (var j = 0; j < aiPrefixes.length; j++) {\n";
    s += "          if (v.indexOf(aiPrefixes[j]) === 0) return aiPrefixes[j];\n";
    s += "        }\n";
    s += "      }\n";
    s += "    } catch (e) { /* no-op */ }\n";
    s += "    return null;\n";
    s += "  }\n";
    s += "  var source = detectSource();\n";
    s += "  if (!source) return;\n";

    if (provider == "plausible") {
        s += "  if (typeof window.plausible === \"function\") {\n";
        s += "    window.plausible(\"AI Referral\", { props: { source: source } });\n";
        s += "  }\n";
    } else if (provider == "umami") {
        s += "  if (window.umami && typeof window.umami.track === \"function\") {\n";
        s += "    window.umami.track(\"ai_referral\", { source: source });\n";
        s += "  }\n";
    } else if (provider == "ga4") {
        s += "  if (typeof window.gtag === \"function\") {\n";
        s += "    window.gtag(\"event\", \"ai_referral\", { source: source });\n";
        s += "  }\n";
    } else if (provider == "custom") {
        // Endpoint is validated as non-empty by the caller.
        s += "  try {\n";
        s += "    fetch(\"" + endpoint + "\", {\n";
        s += "      method: \"POST\",\n";
        s += "      headers: { \"Content-Type\": \"application/json\" },\n";
        s += "      body: JSON.stringify({\n";
        s += "        event: \"ai_referral\",\n";
        s += "        source: source,\n";
        s += "        url: window.location.href,\n";
        s += "        referrer: document.referrer || \"\"\n";
        s += "      })\n";
        s += "    });\n";
        s += "  } catch (e) { /* no-op */ }\n";
    }

    s += "})();\n";
    return s;
}

} // anonymous namespace

std::string build_ai_referrer_snippet(const Config& cfg) {
    if (!cfg.analytics_ai_referrers_enabled) return std::string();

    const std::string& provider = cfg.analytics_ai_referrers_provider;
    if (provider.empty()) {
        std::cerr << utils::warning_label()
                  << " analytics.ai_referrers: 'provider' is empty — set it to "
                     "\"plausible\", \"umami\", \"ga4\", or \"custom\". "
                     "Skipping AI referrer snippet.\n";
        return std::string();
    }

    if (provider == "custom") {
        if (cfg.analytics_ai_referrers_endpoint.empty()) {
            std::cerr << utils::warning_label()
                      << " analytics.ai_referrers: provider=\"custom\" requires "
                         "'endpoint' — set analytics.ai_referrers.endpoint. "
                         "Skipping AI referrer snippet.\n";
            return std::string();
        }
    } else if (provider != "plausible" &&
               provider != "umami" &&
               provider != "ga4") {
        std::cerr << utils::warning_label()
                  << " analytics.ai_referrers: unknown provider '" << provider
                  << "' — expected one of \"plausible\", \"umami\", \"ga4\", "
                     "\"custom\". Skipping AI referrer snippet.\n";
        return std::string();
    }

    return "<script>\n" +
           build_script_body(provider, cfg.analytics_ai_referrers_endpoint) +
           "</script>\n";
}

} // namespace modules
} // namespace cstatic
