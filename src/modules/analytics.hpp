#pragma once

#include <string>

namespace cstatic {

struct Config;

namespace modules {

// Build the AI referrer detection + dispatch <script> block, or empty string
// when cfg.analytics_ai_referrers_enabled is false, the provider is unset or
// unrecognized, or the "custom" provider is selected without an endpoint.
//
// The emitted snippet (wrapped in a single <script>...</script>) is an IIFE
// that:
//   1. Walks a hardcoded list of AI referrer domains against
//      document.referrer's hostname (perplexity.ai, chatgpt.com, ...).
//   2. Falls back to scanning ?utm_source= / ?source= / ?ref= / ?from=
//      URL params for values prefixed with an AI name (perplexity, chatgpt,
//      copilot, gemini, claude, ...).
//   3. Dispatches a provider-specific analytics call:
//        plausible  -> plausible("AI Referral", { props: { source } })
//        umami      -> umami.track("ai_referral", { source })
//        ga4        -> gtag("event", "ai_referral", { source })
//        custom     -> POST {event, source, url, referrer} to endpoint
//      Each dispatch is guarded by the corresponding global's presence so the
//      snippet no-ops gracefully until the provider's loader tag fires.
//
// The snippet is computed once per build (it does not depend on per-page data)
// and exposed to templates as {{ ai_referrer_snippet }}.
std::string build_ai_referrer_snippet(const Config& cfg);

} // namespace modules
} // namespace cstatic
