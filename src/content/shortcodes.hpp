#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

#include <nlohmann/json.hpp>

namespace cstatic {

// Expands `{{< name params >}}` and `{{< name >}}...{{< /name >}}` shortcodes
// in markdown source by rendering the matching `shortcodes/<name>.html`
// template with Inja. The returned string still flows through the normal
// cmark-gfm pass — because CMARK_OPT_UNSAFE is enabled, raw HTML emitted by
// shortcodes passes through to the final document.
//
// Templates are loaded lazily on first use and cached for the lifetime of the
// processor. A missing template produces a one-line warning on stderr and the
// original shortcode text is left in place so authors can see what failed.
//
// Usage:
//   ShortcodeProcessor sc("shortcodes");
//   std::string body = sc.process(markdown_body, page_context_json);
class ShortcodeProcessor {
public:
    explicit ShortcodeProcessor(const std::string& shortcodes_dir);

    // Returns the markdown body with all recognised shortcodes expanded.
    // `page_context` is exposed to shortcode templates as the `page` variable
    // and may be omitted.
    std::string process(const std::string& markdown,
                        const nlohmann::json& page_context = {}) const;

    // True if the shortcodes directory exists and contained at least one
    // readable `.html` file at construction time. When false, process() is
    // a no-op pass-through.
    bool available() const { return available_; }

private:
    // Load (or return cached) template source for `name`. Returns empty
    // string and sets *found=false when no `shortcodes/<name>.html` exists.
    std::string load_template(const std::string& name, bool* found) const;

    // Expand a single shortcode invocation. `name` is the shortcode tag,
    // `params_raw` is the raw text between the tag and the closing `>}}`,
    // `inner` is captured content for block shortcodes (empty for inline).
    std::string render_one(const std::string& name,
                           const std::string& params_raw,
                           const std::string& inner,
                           const nlohmann::json& page_context) const;

    std::string shortcodes_dir_;
    bool available_ = false;
    mutable std::mutex mtx_;
    // Mutable: cache is populated lazily by const process() calls. Guarded by
    // mtx_ because the builder's Phase 1 loop may run multi-threaded.
    mutable std::unordered_map<std::string, std::string> template_cache_;
    mutable std::unordered_map<std::string, bool> missing_cache_;
};

} // namespace cstatic
