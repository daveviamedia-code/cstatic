#pragma once

#include <string>

namespace cstatic::utils {

// ANSI escape codes for terminal color output
namespace color {
    constexpr const char* reset   = "\033[0m";
    constexpr const char* red     = "\033[31m";
    constexpr const char* green   = "\033[32m";
    constexpr const char* yellow  = "\033[33m";
    constexpr const char* cyan    = "\033[36m";
    constexpr const char* bold    = "\033[1m";
    constexpr const char* dim     = "\033[2m";
}

inline bool should_use_color() {
    const char* term = std::getenv("TERM");
    if (!term) return false;
    std::string t(term);
    return t != "dumb";
}

inline std::string colorize(const char* code, const std::string& text) {
    if (!should_use_color()) return text;
    return std::string(code) + text + color::reset;
}

inline std::string error_label()   { return colorize(color::bold, colorize(color::red, "error:")); }
inline std::string warning_label() { return colorize(color::bold, colorize(color::yellow, "warn:")); }
inline std::string info_label()    { return colorize(color::dim, "info:"); }
inline std::string success_label() { return colorize(color::bold, colorize(color::green, "success:")); }
inline std::string notice_label()  { return colorize(color::bold, colorize(color::cyan, "notice:")); }

} // namespace cstatic::utils
