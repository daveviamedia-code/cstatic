#pragma once

#include <string>
#include <vector>

namespace cstatic::utils {

// Simple glob match supporting '*' (matches zero or more of any character).
// A pattern with no '*' matches only on exact equality. Used by the sitemap
// and llms.txt modules to honor `*_exclude` config lists against page URLs.
inline bool glob_match(const std::string& pattern, const std::string& text) {
    auto pit = pattern.begin();
    auto tit = text.begin();

    while (pit != pattern.end() && tit != text.end()) {
        if (*pit == '*') {
            ++pit;
            // '*' matches zero or more of any character
            if (pit == pattern.end()) return true; // trailing * matches rest
            // Find the next occurrence of the char after *
            while (tit != text.end() && *tit != *pit) ++tit;
        } else {
            if (*pit != *tit) return false;
            ++pit;
            ++tit;
        }
    }
    // Consume trailing wildcards
    while (pit != pattern.end() && *pit == '*') ++pit;
    return pit == pattern.end() && tit == text.end();
}

// True if `text` matches any pattern in `patterns`. Empty list never matches.
inline bool matches_any_glob(const std::string& text,
                             const std::vector<std::string>& patterns) {
    for (const auto& p : patterns) {
        if (glob_match(p, text)) return true;
    }
    return false;
}

} // namespace cstatic::utils
