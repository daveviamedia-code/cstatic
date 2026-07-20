#include <catch2/catch_test_macros.hpp>

#include <string>

#include "config/config.hpp"
#include "modules/analytics.hpp"

using cstatic::Config;
using cstatic::modules::build_ai_referrer_snippet;

namespace {

// Mirror of tests/test_well_known.cpp:37-51 — minimal Config with the fields
// build_ai_referrer_snippet reads. No filesystem needed (pure function test).
Config base_config() {
    Config cfg;
    cfg.site_title    = "My Site";
    cfg.site_base_url = "https://example.com";
    return cfg;
}

// Count non-overlapping occurrences of substr in s.
size_t count_occurrences(const std::string& s, const std::string& sub) {
    if (sub.empty()) return 0;
    size_t count = 0, pos = 0;
    while ((pos = s.find(sub, pos)) != std::string::npos) {
        ++count;
        pos += sub.size();
    }
    return count;
}

} // anonymous namespace

TEST_CASE("analytics: disabled by default returns empty string", "[analytics]") {
    Config cfg = base_config();
    REQUIRE(build_ai_referrer_snippet(cfg).empty());
}

TEST_CASE("analytics: plausible provider emits plausible dispatch", "[analytics]") {
    Config cfg = base_config();
    cfg.analytics_ai_referrers_enabled = true;
    cfg.analytics_ai_referrers_provider = "plausible";
    const std::string s = build_ai_referrer_snippet(cfg);
    REQUIRE(!s.empty());
    REQUIRE(s.find("plausible(\"AI Referral\"") != std::string::npos);
    // Guards on window.plausible presence.
    REQUIRE(s.find("typeof window.plausible") != std::string::npos);
}

TEST_CASE("analytics: umami provider emits umami.track dispatch", "[analytics]") {
    Config cfg = base_config();
    cfg.analytics_ai_referrers_enabled = true;
    cfg.analytics_ai_referrers_provider = "umami";
    const std::string s = build_ai_referrer_snippet(cfg);
    REQUIRE(!s.empty());
    REQUIRE(s.find("umami.track(\"ai_referral\"") != std::string::npos);
    REQUIRE(s.find("window.umami") != std::string::npos);
}

TEST_CASE("analytics: ga4 provider emits gtag event dispatch", "[analytics]") {
    Config cfg = base_config();
    cfg.analytics_ai_referrers_enabled = true;
    cfg.analytics_ai_referrers_provider = "ga4";
    const std::string s = build_ai_referrer_snippet(cfg);
    REQUIRE(!s.empty());
    REQUIRE(s.find("gtag(\"event\", \"ai_referral\"") != std::string::npos);
    REQUIRE(s.find("typeof window.gtag") != std::string::npos);
}

TEST_CASE("analytics: custom provider interpolates endpoint URL", "[analytics]") {
    Config cfg = base_config();
    cfg.analytics_ai_referrers_enabled = true;
    cfg.analytics_ai_referrers_provider = "custom";
    cfg.analytics_ai_referrers_endpoint = "https://my.endpoint/ingest";
    const std::string s = build_ai_referrer_snippet(cfg);
    REQUIRE(!s.empty());
    // Endpoint URL appears inside fetch("...").
    REQUIRE(s.find("fetch(\"https://my.endpoint/ingest\"") != std::string::npos);
    REQUIRE(s.find("\"POST\"") != std::string::npos);
    REQUIRE(s.find("\"Content-Type\": \"application/json\"") != std::string::npos);
    // POST body includes the documented fields.
    REQUIRE(s.find("event: \"ai_referral\"") != std::string::npos);
    REQUIRE(s.find("source: source") != std::string::npos);
    REQUIRE(s.find("url: window.location.href") != std::string::npos);
    REQUIRE(s.find("referrer: document.referrer") != std::string::npos);
}

TEST_CASE("analytics: custom provider without endpoint returns empty", "[analytics]") {
    Config cfg = base_config();
    cfg.analytics_ai_referrers_enabled = true;
    cfg.analytics_ai_referrers_provider = "custom";
    // No endpoint set.
    REQUIRE(build_ai_referrer_snippet(cfg).empty());
}

TEST_CASE("analytics: unknown provider returns empty", "[analytics]") {
    Config cfg = base_config();
    cfg.analytics_ai_referrers_enabled = true;
    cfg.analytics_ai_referrers_provider = "adobe-analytics";
    REQUIRE(build_ai_referrer_snippet(cfg).empty());
}

TEST_CASE("analytics: empty provider returns empty", "[analytics]") {
    Config cfg = base_config();
    cfg.analytics_ai_referrers_enabled = true;
    // provider is default-empty.
    REQUIRE(build_ai_referrer_snippet(cfg).empty());
}

TEST_CASE("analytics: every provider snippet contains shared detection list",
          "[analytics]") {
    Config cfg = base_config();
    cfg.analytics_ai_referrers_enabled = true;
    cfg.analytics_ai_referrers_provider = "plausible";
    const std::string s = build_ai_referrer_snippet(cfg);
    REQUIRE(!s.empty());
    // Each known referrer domain appears in the JS array literal.
    for (const char* domain : {
        "perplexity.ai", "chatgpt.com", "copilot.microsoft.com",
        "gemini.google.com", "claude.ai", "you.com",
        "poe.com", "phind.com", "kagi.com"
    }) {
        INFO("checking domain: " << domain);
        REQUIRE(s.find(domain) != std::string::npos);
    }
    // Source-prefix list also present.
    for (const char* prefix : {"perplexity", "chatgpt", "copilot", "gemini", "claude"}) {
        INFO("checking prefix: " << prefix);
        REQUIRE(s.find(std::string("\"") + prefix + "\"") != std::string::npos);
    }
}

TEST_CASE("analytics: snippet is wrapped in exactly one <script> pair", "[analytics]") {
    Config cfg = base_config();
    cfg.analytics_ai_referrers_enabled = true;
    cfg.analytics_ai_referrers_provider = "plausible";
    const std::string s = build_ai_referrer_snippet(cfg);
    REQUIRE(!s.empty());
    REQUIRE(count_occurrences(s, "<script>") == 1);
    REQUIRE(count_occurrences(s, "</script>") == 1);
}

TEST_CASE("analytics: snippet body is an IIFE", "[analytics]") {
    Config cfg = base_config();
    cfg.analytics_ai_referrers_enabled = true;
    cfg.analytics_ai_referrers_provider = "ga4";
    const std::string s = build_ai_referrer_snippet(cfg);
    REQUIRE(!s.empty());
    REQUIRE(s.find("(function(){") != std::string::npos);
    REQUIRE(s.find("})()") != std::string::npos);
}

TEST_CASE("analytics: snippet references URLSearchParams fallback and AI prefixes",
          "[analytics]") {
    Config cfg = base_config();
    cfg.analytics_ai_referrers_enabled = true;
    cfg.analytics_ai_referrers_provider = "umami";
    const std::string s = build_ai_referrer_snippet(cfg);
    REQUIRE(!s.empty());
    // URLSearchParams constructor referenced.
    REQUIRE(s.find("new URLSearchParams(") != std::string::npos);
    // Each documented URL param key appears in the JS array literal.
    for (const char* key : {"utm_source", "source", "ref", "from"}) {
        INFO("checking param key: " << key);
        REQUIRE(s.find(std::string("\"") + key + "\"") != std::string::npos);
    }
    // AI prefix list scan — uses indexOf === 0 (prefix match).
    REQUIRE(s.find("v.indexOf(aiPrefixes[j]) === 0") != std::string::npos);
}
