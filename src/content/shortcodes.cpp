#include "content/shortcodes.hpp"
#include "utils/file_io.hpp"
#include "utils/path.hpp"
#include "utils/terminal.hpp"

#include <inja/inja.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <cctype>
#include <iostream>

namespace cstatic {

namespace fs = std::filesystem;

namespace {

// Trim leading/trailing whitespace.
std::string trim(const std::string& s) {
    auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// Split the raw params string into positional args (`params` array) and
// `key="value"` pairs (`named` object). Single- or double-quoted values are
// supported; quotes are stripped.
void parse_params(const std::string& raw, nlohmann::json& params, nlohmann::json& named) {
    // Tokenize respecting quoted strings.
    std::vector<std::string> tokens;
    std::string current;
    bool in_single = false, in_double = false;
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (in_single) {
            if (c == '\'') { in_single = false; }
            else { current += c; }
        } else if (in_double) {
            if (c == '"') { in_double = false; }
            else { current += c; }
        } else if (c == '\'') {
            in_single = true;
        } else if (c == '"') {
            in_double = true;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) { tokens.push_back(current); current.clear(); }
        } else {
            current += c;
        }
    }
    if (!current.empty()) tokens.push_back(current);

    for (const auto& tok : tokens) {
        auto eq = tok.find('=');
        if (eq != std::string::npos) {
            std::string key = tok.substr(0, eq);
            std::string val = tok.substr(eq + 1);
            // Leading quote was already stripped by the tokenizer (the quote
            // started a string-state). But because we treat `=` as a regular
            // char inside the token loop, a value like `src="x.jpg"` arrives
            // as one token with the quotes intact — strip them here.
            if (val.size() >= 2 &&
                ((val.front() == '"' && val.back() == '"') ||
                 (val.front() == '\'' && val.back() == '\''))) {
                val = val.substr(1, val.size() - 2);
            }
            if (!key.empty()) named[key] = val;
        } else {
            params.push_back(tok);
        }
    }
}

// Build a regex that matches `{{< /name >}}` with flexible whitespace.
std::string close_pattern(const std::string& name) {
    // Escape name (it's \w+ so safe, but be defensive).
    std::string escaped;
    for (char c : name) {
        if (c == '\\' || c == '.' || c == '*' || c == '+' ||
            c == '?' || c == '(' || c == ')' || c == '|' ||
            c == '[' || c == ']' || c == '{' || c == '}' || c == '^' || c == '$') {
            escaped += '\\';
        }
        escaped += c;
    }
    return R"(\{\{<\s*/\s*)" + escaped + R"(\s*>\}\})";
}

} // anonymous namespace

ShortcodeProcessor::ShortcodeProcessor(const std::string& shortcodes_dir)
    : shortcodes_dir_(shortcodes_dir) {
    if (shortcodes_dir_.empty()) {
        available_ = false;
        return;
    }
    fs::path dir(shortcodes_dir_);
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        available_ = false;
        return;
    }
    // Mark available only if at least one .html template is present — this
    // lets the scaffold ship an empty `shortcodes/` directory without paying
    // for regex scanning on every page.
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".html") {
            available_ = true;
            break;
        }
    }
}

std::string ShortcodeProcessor::load_template(const std::string& name, bool* found) const {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = template_cache_.find(name);
        if (it != template_cache_.end()) {
            if (found) *found = true;
            return it->second;
        }
        auto miss = missing_cache_.find(name);
        if (miss != missing_cache_.end()) {
            if (found) *found = false;
            return "";
        }
    }

    fs::path p = fs::path(shortcodes_dir_) / (name + ".html");
    std::string content;
    bool ok = false;
    std::error_code ec;
    if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) {
        std::ifstream f(p);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            content = ss.str();
            ok = true;
        }
    }

    std::lock_guard<std::mutex> lk(mtx_);
    if (ok) {
        template_cache_[name] = content;
    } else {
        missing_cache_[name] = true;
    }
    if (found) *found = ok;
    return content;
}

std::string ShortcodeProcessor::render_one(const std::string& name,
                                           const std::string& params_raw,
                                           const std::string& inner,
                                           const nlohmann::json& page_context) const {
    bool found = false;
    std::string tmpl_source = load_template(name, &found);
    if (!found) {
        std::cerr << utils::notice_label() << " unknown shortcode '"
                  << name << "'\n";
        return "";
    }

    nlohmann::json ctx;
    ctx["content"] = inner;
    nlohmann::json params = nlohmann::json::array();
    nlohmann::json named = nlohmann::json::object();
    parse_params(params_raw, params, named);
    ctx["params"] = params;
    ctx["named"]  = named;
    if (!page_context.is_null()) ctx["page"] = page_context;

    try {
        // Standalone environment — shortcodes do not share state with the
        // main TemplateRenderer and may do their own file includes.
        inja::Environment env;
        auto tmpl = env.parse(tmpl_source);
        return env.render(tmpl, ctx);
    } catch (const inja::InjaError& e) {
        std::cerr << utils::error_label() << " shortcode '" << name
                  << "' render failed: " << e.what() << "\n";
        return "";
    }
}

// Match the opening `{{< name params >}}` (or stray `{{< /name >}}`).
// Captures: group 1 = leading slash (for closing tags), group 2 = name,
// group 3 = remainder (raw params).
static const std::regex& open_re() {
    // Allow optional slash, name, then anything up to >}} (non-greedy).
    static const std::regex re(R"(\{\{<\s*(/?)\s*([A-Za-z_][\w-]*)([^>]*?)\s*>\}\})");
    return re;
}

// Build a regex that matches `{{< name >}}` (no params) — used for nesting
// depth tracking when searching for a balanced close tag of the same name.
std::string block_open_pattern(const std::string& escaped_name) {
    return R"(\{\{<\s*)" + escaped_name + R"(\s*>\}\})";
}

// Escape regex metacharacters in a literal name (defensive — names are \w+).
std::string regex_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\' || c == '.' || c == '*' || c == '+' ||
            c == '?' || c == '(' || c == ')' || c == '|' ||
            c == '[' || c == ']' || c == '{' || c == '}' ||
            c == '^' || c == '$') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

// Result of a balanced-close search: absolute position of the matching close
// tag and the length of the close-tag substring itself.
struct CloseMatch {
    size_t position;
    size_t length;
};

// Given a block opener that ends at `open_end`, find the matching close tag
// that balances any nested same-name block openers. Returns {npos, 0} when
// no balanced close exists.
CloseMatch find_balanced_close(const std::string& md, size_t open_end,
                               const std::string& name) {
    std::string esc = regex_escape(name);
    std::regex open_re(block_open_pattern(esc));
    std::regex close_re(close_pattern(name));
    size_t pos = open_end;
    int depth = 1;
    while (depth > 0) {
        auto search_start = md.begin() + pos;
        std::smatch om, cm;
        bool found_open  = std::regex_search(search_start, md.end(), om, open_re);
        bool found_close = std::regex_search(search_start, md.end(), cm, close_re);
        size_t open_pos  = found_open  ? pos + om.position() : std::string::npos;
        size_t close_pos = found_close ? pos + cm.position() : std::string::npos;
        if (close_pos == std::string::npos) return {std::string::npos, 0};
        if (open_pos != std::string::npos && open_pos < close_pos) {
            // Nested same-name block opener: skip past it and keep searching.
            depth++;
            pos = open_pos + om[0].length();
        } else {
            depth--;
            if (depth == 0) return {close_pos, static_cast<size_t>(cm[0].length())};
            pos = close_pos + cm[0].length();
        }
    }
    return {std::string::npos, 0};
}

std::string ShortcodeProcessor::process(const std::string& markdown,
                                        const nlohmann::json& page_context) const {
    if (!available_ || markdown.empty()) return markdown;

    const std::regex& re = open_re();
    std::string out;
    out.reserve(markdown.size());

    size_t pos = 0;
    while (pos < markdown.size()) {
        // Find next shortcode opener at or after `pos`.
        std::smatch m;
        auto search_start = markdown.begin() + pos;
        if (!std::regex_search(search_start, markdown.end(), m, re)) {
            out.append(markdown, pos, std::string::npos);
            break;
        }
        size_t match_pos = pos + m.position();

        // Append literal text preceding the match.
        out.append(markdown, pos, match_pos - pos);

        const std::string slash = m[1].str();
        const std::string name  = m[2].str();
        const std::string rest  = trim(m[3].str());

        // Stray closing tag — leave verbatim.
        if (!slash.empty()) {
            out += m[0].str();
            pos = match_pos + m[0].length();
            continue;
        }

        size_t advance_to = match_pos + m[0].length();
        std::string rendered;
        bool consumed_block = false;

        // Block opener only when no params were supplied.
        if (rest.empty()) {
            CloseMatch cm = find_balanced_close(markdown, advance_to, name);
            if (cm.position != std::string::npos) {
                std::string inner(markdown, advance_to,
                                  cm.position - advance_to);
                std::string processed_inner = process(inner, page_context);
                rendered = render_one(name, "", processed_inner, page_context);
                advance_to = cm.position + cm.length;
                consumed_block = true;
            }
        }

        if (!consumed_block) {
            rendered = render_one(name, rest, "", page_context);
        }

        out += rendered;
        pos = advance_to;
    }
    return out;
}

} // namespace cstatic
