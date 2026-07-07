#pragma once

// Shared slugify utility. Extracted from link_graph.cpp so that G8 (passage
// index) and G11 (auto TOC) produce IDs identical to the wikilink resolver —
// anchor links match passage IDs because they share this algorithm.
//
// Slugify: lowercase, keep alphanumerics, convert spaces/underscores to
// hyphens, collapse runs of hyphens, strip leading/trailing hyphens.

#include <cctype>
#include <string>

namespace cstatic::utils {

inline std::string slugify(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool prev_hyphen = false;
    for (char c : s) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            out += static_cast<char>(std::tolower(uc));
            prev_hyphen = false;
        } else if (c == ' ' || c == '_' || c == '-') {
            if (!prev_hyphen && !out.empty()) {
                out += '-';
                prev_hyphen = true;
            }
        }
        // other punctuation: skip
    }
    // strip trailing hyphen (loop above already prevents runs)
    if (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

} // namespace cstatic::utils
