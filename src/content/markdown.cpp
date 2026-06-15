#include "content/markdown.hpp"

#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cstatic {

namespace {

// ---------------------------------------------------------------------------
// HTML escape / unescape helpers
// ---------------------------------------------------------------------------

std::string html_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;
        }
    }
    return out;
}

void replace_all(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

// Decode the common HTML entities cmark emits in code blocks.
std::string html_unescape(const std::string& s) {
    std::string out = s;
    replace_all(out, "&lt;",   "<");
    replace_all(out, "&gt;",   ">");
    replace_all(out, "&quot;", "\"");
    replace_all(out, "&#39;",  "'");
    replace_all(out, "&amp;",  "&"); // last to avoid double-decoding
    return out;
}

bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '$';
}

// ---------------------------------------------------------------------------
// Language configuration
// ---------------------------------------------------------------------------

struct LangConfig {
    std::string keywords;          // space-separated list
    std::string builtins;          // space-separated list
    std::string line_comment;      // e.g. "//" or "#"; empty if none
    std::string block_open;        // e.g. "/*"; empty if none
    std::string block_close;       // e.g. "*/"
    std::string string_chars;      // chars that start strings, e.g. "\"'`"
};

const LangConfig& lang_config(const std::string& lang) {
    static const std::unordered_map<std::string, LangConfig> table = {
        {"js",   {"var let const function return if else for while do switch case break continue "
                  "new delete typeof instanceof void in of this super class extends import export "
                  "from default try catch finally throw async await yield static get set "
                  "true false null undefined NaN Infinity",
                  "console Math JSON Object Array String Number Boolean Date Promise Map Set "
                  "parseInt parseFloat isNaN",
                  "//", "/*", "*/", "\"'`"}},
        {"javascript", {"var let const function return if else for while do switch case break continue "
                  "new delete typeof instanceof void in of this super class extends import export "
                  "from default try catch finally throw async await yield static get set "
                  "true false null undefined NaN Infinity",
                  "console Math JSON Object Array String Number Boolean Date Promise Map Set "
                  "parseInt parseFloat isNaN",
                  "//", "/*", "*/", "\"'`"}},
        {"ts",   {"var let const function return if else for while do switch case break continue "
                  "new delete typeof instanceof void in of this super class extends import export "
                  "from default try catch finally throw async await yield static get set "
                  "interface type enum namespace public private protected readonly "
                  "true false null undefined",
                  "console Math JSON Object Array String Number Boolean Date Promise Map Set",
                  "//", "/*", "*/", "\"'`"}},
        {"py",   {"def class return if elif else for while break continue pass raise try except finally "
                  "with as import from global nonlocal lambda yield assert del in is not and or "
                  "None True False self",
                  "print len range str int float bool list dict tuple set open type isinstance "
                  "super Exception",
                  "#", "", "", "\"'"}},
        {"python", {"def class return if elif else for while break continue pass raise try except finally "
                  "with as import from global nonlocal lambda yield assert del in is not and or "
                  "None True False self",
                  "print len range str int float bool list dict tuple set open type isinstance "
                  "super Exception",
                  "#", "", "", "\"'"}},
        {"cpp",  {"int long short char void bool float double unsigned signed const static "
                  "auto register volatile inline virtual explicit friend mutable constexpr "
                  "class struct union enum namespace using typename template typedef "
                  "public private protected return if else for while do switch case default break "
                  "continue new delete this throw try catch noexcept operator sizeof "
                  "true false nullptr std",
                  "std string vector map set cout cin endl size_t",
                  "//", "/*", "*/", "\"'"}},
        {"c++",  {"int long short char void bool float double unsigned signed const static "
                  "auto register volatile inline virtual explicit friend mutable constexpr "
                  "class struct union enum namespace using typename template typedef "
                  "public private protected return if else for while do switch case default break "
                  "continue new delete this throw try catch noexcept operator sizeof "
                  "true false nullptr std",
                  "std string vector map set cout cin endl size_t",
                  "//", "/*", "*/", "\"'"}},
        {"c",    {"int long short char void float double unsigned signed const static auto register "
                  "volatile inline struct union enum typedef return if else for while do switch case "
                  "default break continue sizeof NULL",
                  "printf scanf malloc free sizeof",
                  "//", "/*", "*/", "\"'"}},
        {"go",   {"break case chan const continue default defer else fallthrough for func go goto "
                  "if import interface map package range return select struct switch type var "
                  "true false nil iota",
                  "make len cap append print println panic recover",
                  "//", "/*", "*/", "\"'`"}},
        {"rs",   {"fn let mut pub struct enum trait impl use mod match if else for while loop "
                  "return as ref move static const unsafe async await dyn crate super self Self "
                  "true false",
                  "println vec String Vec Option Result Box Rc Arc",
                  "//", "/*", "*/", "\"'"}},
        {"rust", {"fn let mut pub struct enum trait impl use mod match if else for while loop "
                  "return as ref move static const unsafe async await dyn crate super self Self "
                  "true false",
                  "println vec String Vec Option Result Box Rc Arc",
                  "//", "/*", "*/", "\"'"}},
        {"sh",   {"if then else elif fi for while do done case esac function in return exit local "
                  "export unset echo read source",
                  "echo read cd ls cat grep sed awk cp mv rm mkdir",
                  "#", "", "", "\"'"}},
        {"bash", {"if then else elif fi for while do done case esac function in return exit local "
                  "export unset echo read source",
                  "echo read cd ls cat grep sed awk cp mv rm mkdir",
                  "#", "", "", "\"'"}},
        {"shell",{"if then else elif fi for while do done case esac function in return exit local "
                  "export unset echo read source",
                  "echo read cd ls cat grep sed awk cp mv rm mkdir",
                  "#", "", "", "\"'"}},
        {"json", {"true false null", "", "", "", "", "\"'"}},
        {"yaml", {"true false null yes no on off", "", "#", "", "", "\"'"}},
        {"yml",  {"true false null yes no on off", "", "#", "", "", "\"'"}},
    };

    static const LangConfig empty;
    auto it = table.find(lang);
    return (it != table.end()) ? it->second : empty;
}

// Split a space-separated string into a vector of tokens.
std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        while (i < n && std::isspace((unsigned char)s[i])) i++;
        size_t start = i;
        while (i < n && !std::isspace((unsigned char)s[i])) i++;
        if (i > start) out.emplace_back(s.substr(start, i - start));
    }
    return out;
}

bool in_list(const std::string& w, const std::unordered_set<std::string>& set) {
    return set.count(w) > 0;
}

// ---------------------------------------------------------------------------
// Generic tokenizer for C-like / scripting languages
// ---------------------------------------------------------------------------

std::string tokenize_generic(const std::string& code, const LangConfig& lang) {
    std::unordered_set<std::string> keywords;
    for (auto& k : split_ws(lang.keywords)) keywords.insert(k);
    std::unordered_set<std::string> builtins;
    for (auto& b : split_ws(lang.builtins)) builtins.insert(b);

    std::string out;
    out.reserve(code.size() * 2);
    std::string plain;
    auto flush_plain = [&]() {
        if (!plain.empty()) { out += html_escape(plain); plain.clear(); }
    };

    size_t i = 0, n = code.size();
    while (i < n) {
        char c = code[i];

        // Block comment
        if (!lang.block_open.empty() &&
            i + lang.block_open.size() <= n &&
            code.compare(i, lang.block_open.size(), lang.block_open) == 0) {
            flush_plain();
            size_t end = code.find(lang.block_close, i + lang.block_open.size());
            size_t stop = (end == std::string::npos) ? n : end + lang.block_close.size();
            out += "<span class=\"hl-comment\">" + html_escape(code.substr(i, stop - i)) + "</span>";
            i = stop;
            continue;
        }

        // Line comment
        if (!lang.line_comment.empty() &&
            i + lang.line_comment.size() <= n &&
            code.compare(i, lang.line_comment.size(), lang.line_comment) == 0) {
            flush_plain();
            size_t end = code.find('\n', i);
            size_t stop = (end == std::string::npos) ? n : end;
            out += "<span class=\"hl-comment\">" + html_escape(code.substr(i, stop - i)) + "</span>";
            i = stop;
            continue;
        }

        // String
        if (lang.string_chars.find(c) != std::string::npos) {
            flush_plain();
            size_t end = i + 1;
            while (end < n) {
                if (code[end] == '\\' && end + 1 < n) { end += 2; continue; }
                if (code[end] == c) { end++; break; }
                end++;
            }
            out += "<span class=\"hl-string\">" + html_escape(code.substr(i, end - i)) + "</span>";
            i = end;
            continue;
        }

        // Number (not part of a larger identifier)
        if (std::isdigit((unsigned char)c) && (plain.empty() || !is_word_char(plain.back()))) {
            flush_plain();
            size_t end = i;
            while (end < n && (std::isalnum((unsigned char)code[end]) ||
                               code[end] == '.' || code[end] == '_')) end++;
            out += "<span class=\"hl-number\">" + html_escape(code.substr(i, end - i)) + "</span>";
            i = end;
            continue;
        }

        // Word: keyword / builtin / function / identifier
        if (is_word_char(c)) {
            size_t end = i;
            while (end < n && is_word_char(code[end])) end++;
            std::string word = code.substr(i, end - i);

            // Look ahead past spaces for '('
            size_t la = end;
            while (la < n && (code[la] == ' ' || code[la] == '\t')) la++;
            bool is_call = (la < n && code[la] == '(');

            flush_plain();
            if (in_list(word, keywords)) {
                out += "<span class=\"hl-keyword\">" + html_escape(word) + "</span>";
            } else if (in_list(word, builtins)) {
                out += "<span class=\"hl-built_in\">" + html_escape(word) + "</span>";
            } else if (is_call) {
                out += "<span class=\"hl-function\">" + html_escape(word) + "</span>";
            } else {
                out += html_escape(word);
            }
            i = end;
            continue;
        }

        plain += c;
        i++;
    }
    flush_plain();
    return out;
}

// ---------------------------------------------------------------------------
// HTML / CSS dedicated tokenizers (work on unescaped source)
// ---------------------------------------------------------------------------

std::string tokenize_html(const std::string& code) {
    std::string out;
    out.reserve(code.size() * 2);
    size_t i = 0, n = code.size();

    while (i < n) {
        // Comment
        if (code.compare(i, 4, "<!--") == 0) {
            size_t end = code.find("-->", i + 4);
            size_t stop = (end == std::string::npos) ? n : end + 3;
            out += "<span class=\"hl-comment\">" + html_escape(code.substr(i, stop - i)) + "</span>";
            i = stop;
            continue;
        }
        // Tag
        if (code[i] == '<') {
            size_t end = code.find('>', i);
            if (end == std::string::npos) {
                out += html_escape(code.substr(i));
                break;
            }
            end++; // include '>'
            std::string tag = code.substr(i, end - i);
            out += "<span class=\"hl-keyword\">" + html_escape(tag) + "</span>";
            i = end;
            continue;
        }
        // Plain text up to next '<'
        size_t next = code.find('<', i);
        size_t stop = (next == std::string::npos) ? n : next;
        out += html_escape(code.substr(i, stop - i));
        i = stop;
    }
    return out;
}

std::string tokenize_css(const std::string& code) {
    std::string out;
    out.reserve(code.size() * 2);
    size_t i = 0, n = code.size();

    while (i < n) {
        // Comment
        if (code.compare(i, 2, "/*") == 0) {
            size_t end = code.find("*/", i + 2);
            size_t stop = (end == std::string::npos) ? n : end + 2;
            out += "<span class=\"hl-comment\">" + html_escape(code.substr(i, stop - i)) + "</span>";
            i = stop;
            continue;
        }
        // Selector up to '{'
        size_t brace = code.find('{', i);
        if (brace == std::string::npos) {
            out += html_escape(code.substr(i));
            break;
        }
        std::string selector = code.substr(i, brace - i);
        out += "<span class=\"hl-keyword\">" + html_escape(selector) + "</span>";
        out += html_escape("{");
        i = brace + 1;

        // Declarations up to '}'
        size_t close = code.find('}', i);
        size_t dstop = (close == std::string::npos) ? n : close;
        std::string decls = code.substr(i, dstop - i);
        // Highlight property names and strings inside declarations
        {
            std::string d;
            size_t j = 0, dn = decls.size();
            while (j < dn) {
                char c = decls[j];
                if (c == '"' || c == '\'') {
                    size_t end = j + 1;
                    while (end < dn && decls[end] != c) end++;
                    if (end < dn) end++;
                    d += "<span class=\"hl-string\">" + html_escape(decls.substr(j, end - j)) + "</span>";
                    j = end;
                    continue;
                }
                // property name before ':'
                size_t colon = decls.find(':', j);
                size_t semi  = decls.find(';', j);
                if (colon < dn && (semi == std::string::npos || colon < semi)) {
                    std::string prop = decls.substr(j, colon - j);
                    // trim trailing space
                    d += "<span class=\"hl-attr\">" + html_escape(prop) + "</span>";
                    j = colon;
                } else {
                    d += html_escape(std::string(1, c));
                    j++;
                }
            }
            out += d;
        }
        i = dstop;
        if (i < n && code[i] == '}') { out += html_escape("}"); i++; }
    }
    return out;
}

// Tokenize one code block for the given language. Returns highlighted HTML.
std::string highlight_for_language(const std::string& code, const std::string& lang) {
    if (lang == "html" || lang == "xml") return tokenize_html(code);
    if (lang == "css" || lang == "scss") return tokenize_css(code);

    const LangConfig& cfg = lang_config(lang);
    if (cfg.line_comment.empty() && cfg.block_open.empty() && cfg.string_chars.empty() &&
        cfg.keywords.empty()) {
        // Unknown language — just escape, no highlighting.
        return html_escape(code);
    }
    return tokenize_generic(code, cfg);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string highlight_code_blocks(const std::string& html, const std::string& /*style*/) {
    // Match <pre><code [class="language-X"]>...</code></pre>
    static const std::regex code_re(
        R"RE(<pre><code(?:\s+class="language-([\w+-]+)")?>([\s\S]*?)</code></pre>)RE"
    );

    std::string result;
    result.reserve(html.size());
    auto begin = std::sregex_iterator(html.begin(), html.end(), code_re);
    auto end   = std::sregex_iterator();

    size_t last = 0;
    for (std::sregex_iterator it = begin; it != end; ++it) {
        result.append(html, last, it->position() - last);

        std::string lang = (*it)[1].str();
        std::string escaped_code = (*it)[2].str();
        std::string raw = html_unescape(escaped_code);

        std::string body;
        if (lang.empty()) {
            body = html_escape(raw); // no language hint — plain
        } else {
            body = highlight_for_language(raw, lang);
        }

        std::string cls = lang.empty() ? "" : (" class=\"language-" + lang + "\"");
        result += "<pre><code" + cls + ">" + body + "</code></pre>";

        last = it->position() + it->length();
    }
    result.append(html, last, std::string::npos);
    return result;
}

std::string highlight_css(const std::string& style) {
    if (style == "github-dark") {
        return "/* C-Static highlight theme: github-dark */\n"
               "pre code{display:block;overflow-x:auto;padding:1em;"
               "background:#0d1117;color:#c9d1d9;border-radius:6px;}\n"
               "pre code .hl-comment{color:#8b949e;}\n"
               "pre code .hl-keyword{color:#ff7b72;}\n"
               "pre code .hl-string{color:#a5d6ff;}\n"
               "pre code .hl-number{color:#79c0ff;}\n"
               "pre code .hl-function{color:#d2a8ff;}\n"
               "pre code .hl-built_in{color:#ffa657;}\n"
               "pre code .hl-attr{color:#79c0ff;}\n";
    }
    // Default: github (light)
    return "/* C-Static highlight theme: github */\n"
           "pre code{display:block;overflow-x:auto;padding:1em;"
           "background:#f6f8fa;color:#24292e;border-radius:6px;}\n"
           "pre code .hl-comment{color:#6a737d;}\n"
           "pre code .hl-keyword{color:#d73a49;}\n"
           "pre code .hl-string{color:#032f62;}\n"
           "pre code .hl-number{color:#005cc5;}\n"
           "pre code .hl-function{color:#6f42c1;}\n"
           "pre code .hl-built_in{color:#005cc5;}\n"
           "pre code .hl-attr{color:#005cc5;}\n";
}

std::string render_markdown(const std::string& markdown) {
    MarkdownOptions opts;
    return render_markdown(markdown, opts);
}

std::string render_markdown(const std::string& markdown, const MarkdownOptions& opts) {
    // Determine which GFM extensions to enable. Empty = all four.
    std::vector<std::string> wanted = opts.extensions;
    if (wanted.empty()) {
        wanted = {"table", "tasklist", "strikethrough", "autolink"};
    }

    // Register core extensions (idempotent).
    cmark_gfm_core_extensions_ensure_registered();

    int options = CMARK_OPT_UNSAFE | CMARK_OPT_SMART;

    cmark_parser* parser = cmark_parser_new(options);

    // Attach the requested syntax extensions to the parser.
    cmark_llist* render_exts = nullptr;
    cmark_mem* mem = cmark_get_default_mem_allocator();
    for (const auto& name : wanted) {
        cmark_syntax_extension* ext = cmark_find_syntax_extension(name.c_str());
        if (ext) {
            cmark_parser_attach_syntax_extension(parser, ext);
            render_exts = cmark_llist_append(mem, render_exts, ext);
        }
    }

    cmark_parser_feed(parser, markdown.c_str(), markdown.size());
    cmark_node* doc = cmark_parser_finish(parser);

    char* html = cmark_render_html(doc, options, render_exts);
    std::string result(html ? html : "");

    free(html);
    cmark_llist_free(mem, render_exts);
    cmark_node_free(doc);
    cmark_parser_free(parser);

    if (opts.highlight_enabled) {
        result = highlight_code_blocks(result, opts.highlight_style);
    }
    return result;
}

} // namespace cstatic
