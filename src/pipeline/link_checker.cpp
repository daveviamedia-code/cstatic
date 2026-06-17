#include "pipeline/link_checker.hpp"
#include "utils/terminal.hpp"

#include <httplib.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cstatic {
namespace pipeline {

namespace fs = std::filesystem;

namespace {

// A URL/line pair extracted from an HTML document.
struct FoundLink {
    std::string url;
    int         line;
};

// Extract every href="..." / src='...' value from an HTML document.
// Matches both quote styles, case-insensitively. Line numbers are 1-based
// and computed from the newline count from the document start to the match.
std::vector<FoundLink> extract_links(const std::string& html) {
    std::vector<FoundLink> out;
    static const std::regex re(R"((?:href|src)\s*=\s*["']([^"']*)["'])",
                               std::regex::icase);
    const auto begin = html.cbegin();
    const auto end   = html.cend();
    std::smatch m;
    auto pos = begin;
    while (std::regex_search(pos, end, m, re)) {
        int line = static_cast<int>(std::count(begin, m[0].first, '\n')) + 1;
        out.push_back({m[1].str(), line});
        pos = m[0].second;
    }
    return out;
}

// True for URLs we never verify: anchors, special schemes, empty values.
bool is_skippable(const std::string& url) {
    auto first = url.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return true;       // empty / whitespace only
    if (url[first] == '#') return true;                 // in-page anchor
    // Lowercase a prefix to test against scheme prefixes.
    std::string lower;
    lower.reserve(16);
    for (size_t i = first; i < url.size() && lower.size() < 16; ++i) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(url[i]))));
    }
    static const char* const schemes[] = {
        "mailto:", "tel:", "sms:", "fax:", "data:",
        "javascript:", "callto:", "skype:", "facetime:"
    };
    for (const char* s : schemes) {
        if (lower.rfind(s, 0) == 0) return true;
    }
    return false;
}

// http://, https://, or protocol-relative //host
bool is_external(const std::string& url) {
    return url.rfind("http://", 0) == 0
        || url.rfind("https://", 0) == 0
        || url.rfind("//", 0) == 0;
}

// Strip ?query and #fragment from a path portion.
std::string strip_fragment_query(const std::string& s) {
    std::string out = s;
    auto q = out.find('?');
    if (q != std::string::npos) out.resize(q);
    auto h = out.find('#');
    if (h != std::string::npos) out.resize(h);
    return out;
}

// Resolve a root-relative URL (starts with '/') to an absolute output path.
// - Trailing '/' or no extension → treat as a directory, append index.html.
std::string resolve_internal(const std::string& output_root, const std::string& url) {
    std::string path = strip_fragment_query(url);
    if (!path.empty() && path[0] == '/') path.erase(0, 1);  // drop leading '/'

    if (path.empty() || path.back() == '/') {
        path += "index.html";
    } else {
        // If the last segment has no '.', treat the path as a directory.
        auto slash = path.rfind('/');
        std::string last = (slash == std::string::npos) ? path : path.substr(slash + 1);
        if (last.find('.') == std::string::npos) {
            path += "/index.html";
        }
    }
    fs::path resolved = fs::path(output_root) / path;
    return fs::weakly_canonical(resolved).string();
}

// Outcome of probing one external URL. transport_ok=false means no HTTP
// response was received (DNS/connection/timeout/SSL failure) — reported as
// a warning, NOT a broken-link issue. status>=400 is a real issue.
struct HttpOutcome {
    bool        transport_ok = false;
    int         status = 0;
    std::string message;
};

HttpOutcome check_external_url(const std::string& url, int timeout_ms) {
    HttpOutcome out;

    std::string scheme, rest;
    if (url.rfind("//", 0) == 0)        { scheme = "https"; rest = url.substr(2); }
    else if (url.rfind("http://", 0) == 0)  { scheme = "http";  rest = url.substr(7); }
    else if (url.rfind("https://", 0) == 0) { scheme = "https"; rest = url.substr(8); }
    else { out.message = "unrecognized URL scheme"; return out; }

    auto frag = rest.find('#');
    if (frag != std::string::npos) rest.resize(frag);

    std::string host_port, path_query;
    auto slash = rest.find('/');
    if (slash == std::string::npos) { host_port = rest; path_query = "/"; }
    else                            { host_port = rest.substr(0, slash); path_query = rest.substr(slash); }

    if (host_port.empty()) { out.message = "missing host"; return out; }

    httplib::Client cli(scheme + "://" + host_port);
    cli.set_follow_location(true);
    if (timeout_ms <= 0) timeout_ms = 5000;
    time_t sec = static_cast<time_t>((timeout_ms + 999) / 1000);
    if (sec < 1) sec = 1;
    cli.set_connection_timeout(sec);
    cli.set_read_timeout(sec);

    if (!cli.is_valid()) {
        out.message = "could not initialize HTTP client";
        return out;
    }

    // HEAD first (cheaper). Fall back to GET when the server signals that
    // HEAD is not allowed (405) or not implemented (501).
    auto head_res = cli.Head(path_query);
    if (head_res) {
        int st = head_res->status;
        if (st == 405 || st == 501) {
            auto get_res = cli.Get(path_query);
            if (get_res) {
                out.transport_ok = true;
                out.status = get_res->status;
                if (get_res->status >= 400) out.message = "HTTP " + std::to_string(get_res->status);
                return out;
            }
            out.message = "GET transport error";
            return out;
        }
        out.transport_ok = true;
        out.status = st;
        if (st >= 400) out.message = "HTTP " + std::to_string(st);
        return out;
    }
    out.message = "HEAD transport error";
    return out;
}

} // anonymous namespace

CheckResult check_links(const std::string& output_dir,
                        bool check_external,
                        int  timeout_ms) {
    CheckResult result;

    std::error_code ec;
    fs::path output_root = fs::weakly_canonical(fs::path(output_dir), ec);
    if (ec || !fs::is_directory(output_root, ec)) {
        LinkIssue issue;
        issue.source_file = output_dir;
        issue.message = "output directory does not exist or is not a directory";
        result.issues.push_back(issue);
        return result;
    }
    const std::string output_root_str = output_root.string();

    // One filesystem scan; subsequent lookups are O(1).
    std::unordered_set<std::string> valid_paths;
    for (auto it = fs::recursive_directory_iterator(output_root, ec);
         it != fs::recursive_directory_iterator(); ) {
        if (ec) { ec.clear(); it.increment(ec); continue; }
        std::error_code rec_ec;
        if (it->is_regular_file(rec_ec) || it->is_symlink(rec_ec)) {
            valid_paths.insert(fs::weakly_canonical(it->path(), ec).string());
        }
        it.increment(ec);
    }

    // Dedupe cache: each unique external URL is hit at most once per run.
    std::unordered_map<std::string, HttpOutcome> external_cache;

    for (auto it = fs::recursive_directory_iterator(output_root, ec);
         it != fs::recursive_directory_iterator(); ) {
        if (ec) { ec.clear(); it.increment(ec); continue; }
        std::error_code rec_ec;
        if (!it->is_regular_file(rec_ec)) { it.increment(ec); continue; }
        if (it->path().extension() != ".html") { it.increment(ec); continue; }

        const std::string file_abs = fs::weakly_canonical(it->path(), ec).string();
        std::ifstream f(file_abs);
        if (!f.is_open()) { it.increment(ec); continue; }
        std::string html((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

        for (const auto& link : extract_links(html)) {
            ++result.total_links;
            if (is_skippable(link.url)) continue;

            if (is_external(link.url)) {
                if (!check_external) continue;
                HttpOutcome oc;
                auto found = external_cache.find(link.url);
                if (found != external_cache.end()) {
                    oc = found->second;
                } else {
                    oc = check_external_url(link.url, timeout_ms);
                    external_cache.emplace(link.url, oc);
                    ++result.external_checked;
                    if (!oc.transport_ok) {
                        std::cerr << cstatic::utils::warning_label()
                                  << " " << link.url << ": " << oc.message << "\n";
                    }
                }
                if (oc.transport_ok && oc.status >= 400) {
                    LinkIssue issue;
                    issue.source_file = file_abs;
                    issue.line        = link.line;
                    issue.href        = link.url;
                    issue.message     = oc.message.empty()
                                            ? ("HTTP " + std::to_string(oc.status))
                                            : oc.message;
                    issue.is_external = true;
                    result.issues.push_back(issue);
                }
                continue;
            }

            // Root-relative internal link.
            if (!link.url.empty() && link.url[0] == '/') {
                ++result.internal_checked;
                std::string target = resolve_internal(output_root_str, link.url);
                if (valid_paths.find(target) == valid_paths.end()) {
                    LinkIssue issue;
                    issue.source_file = file_abs;
                    issue.line        = link.line;
                    issue.href        = link.url;
                    issue.message     = "broken internal link → " + target;
                    result.issues.push_back(issue);
                }
            }
            // else: relative URL — out of scope for v1.
        }
        it.increment(ec);
    }

    return result;
}

} // namespace pipeline
} // namespace cstatic
