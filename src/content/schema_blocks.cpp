#include "content/schema_blocks.hpp"
#include "content/markdown.hpp"
#include "utils/file_io.hpp"
#include "utils/terminal.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cstatic {

namespace {

// Trim all leading/trailing ASCII whitespace (spaces, tabs, newlines).
std::string trim_ws(const std::string& s) {
    auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// Escape a literal string for safe interpolation into HTML text/attributes.
std::string escape_html(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            default:   out += c;
        }
    }
    return out;
}

// Escape regex metacharacters in a literal marker such as `##?`.
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

// Parse `key="value"` pairs out of a raw attribute string. Single quotes and
// unquoted values are not supported (schema attrs are always double-quoted).
std::map<std::string, std::string> parse_attrs(const std::string& s) {
    std::map<std::string, std::string> out;
    static const std::regex re(R"RE((\w+)\s*=\s*"([^"]*)")RE");
    for (std::sregex_iterator it(s.begin(), s.end(), re), end; it != end; ++it) {
        out[(*it)[1].str()] = (*it)[2].str();
    }
    return out;
}

// Walk lines of `text`, splitting on `<marker>` headings (e.g. `##?` or `##!`).
// Each heading's text is the title; everything until the next heading (or end)
// is its body. Returns (title, body_md) pairs in source order.
std::vector<std::pair<std::string, std::string>>
extract_marker_pairs(const std::string& text, const std::string& marker) {
    std::vector<std::pair<std::string, std::string>> out;
    const std::regex re("^" + regex_escape(marker) + R"([ \t]+(.+)$)");
    std::istringstream ss(text);
    std::string line;
    std::string title, body;
    bool have = false;
    auto flush = [&]() {
        if (have) {
            out.emplace_back(title, trim_ws(body));
        }
        title.clear();
        body.clear();
        have = false;
    };
    while (std::getline(ss, line)) {
        // Normalize trailing CR (CRLF inputs).
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::smatch m;
        if (std::regex_match(line, m, re)) {
            flush();
            title = m[1].str();
            have = true;
        } else if (have) {
            body += line;
            body += "\n";
        }
    }
    flush();
    return out;
}

// Render FAQPage: `##? question` headings → Question/AcceptedAnswer schema and
// a <section class="faq"><details>… visible block. Emits no schema (and passes
// the content through) when the block contains no questions.
std::string render_faq(const std::string& inner,
                       std::vector<nlohmann::json>& out_schemas) {
    auto pairs = extract_faq_pairs(inner);
    if (pairs.empty()) {
        std::cerr << utils::warning_label()
                  << " FAQPage schema block has no '##?' questions"
                     " — passing content through\n";
        return inner;
    }

    nlohmann::json entity = nlohmann::json::array();
    std::string html = "<section class=\"faq\">\n";
    for (const auto& p : pairs) {
        std::string ans_html  = render_markdown(p.answer_md);
        std::string ans_text  = trim_ws(utils::strip_html_tags(ans_html));

        nlohmann::json q;
        q["@type"] = "Question";
        q["name"]  = p.question;
        nlohmann::json a;
        a["@type"] = "Answer";
        a["text"]  = ans_text;
        q["acceptedAnswer"] = a;
        entity.push_back(q);

        html += "<details>\n";
        html += "<summary>" + escape_html(p.question) + "</summary>\n";
        if (!ans_html.empty()) html += ans_html + "\n";
        html += "</details>\n";
    }
    html += "</section>";

    nlohmann::json schema;
    schema["@context"]   = "https://schema.org";
    schema["@type"]      = "FAQPage";
    schema["mainEntity"] = entity;
    out_schemas.push_back(std::move(schema));
    return html;
}

// Render HowTo: `##! step title` headings → HowToStep schema and a
// <ol class="howto"> visible block.
std::string render_howto(const std::string& inner,
                         std::vector<nlohmann::json>& out_schemas) {
    auto steps = extract_marker_pairs(inner, "##!");
    if (steps.empty()) {
        std::cerr << utils::warning_label()
                  << " HowTo schema block has no '##!' steps"
                     " — passing content through\n";
        return inner;
    }

    nlohmann::json step_arr = nlohmann::json::array();
    std::string html = "<ol class=\"howto\">\n";
    for (const auto& s : steps) {
        std::string text_html   = render_markdown(s.second);
        std::string text_plain  = trim_ws(utils::strip_html_tags(text_html));

        nlohmann::json step;
        step["@type"] = "HowToStep";
        step["name"]  = s.first;
        step["text"]  = text_plain;
        step_arr.push_back(step);

        html += "<li>\n";
        html += "<h3>" + escape_html(s.first) + "</h3>\n";
        if (!text_html.empty()) html += text_html + "\n";
        html += "</li>\n";
    }
    html += "</ol>";

    nlohmann::json schema;
    schema["@context"] = "https://schema.org";
    schema["@type"]    = "HowTo";
    schema["step"]     = step_arr;
    out_schemas.push_back(std::move(schema));
    return html;
}

// Render Review: attrs `item="…"` / `rating="N"` plus the block body as the
// review text → Review schema and a <div class="review" data-rating="N"> block.
std::string render_review(const std::string& attrs, const std::string& inner,
                          std::vector<nlohmann::json>& out_schemas) {
    auto named = parse_attrs(attrs);
    std::string item   = named.count("item")   ? named["item"]   : "";
    std::string rating = named.count("rating") ? named["rating"] : "";

    std::string body_md   = trim_ws(inner);
    std::string body_html = body_md.empty() ? "" : render_markdown(body_md);
    std::string body_text = trim_ws(utils::strip_html_tags(body_html));

    nlohmann::json schema;
    schema["@context"] = "https://schema.org";
    schema["@type"]    = "Review";
    if (!body_text.empty()) schema["reviewBody"] = body_text;
    if (!item.empty()) {
        nlohmann::json ir;
        ir["@type"] = "Thing";
        ir["name"]  = item;
        schema["itemReviewed"] = ir;
    }
    if (!rating.empty()) {
        nlohmann::json rr;
        rr["@type"]       = "Rating";
        rr["ratingValue"] = rating;
        schema["reviewRating"] = rr;
    }
    out_schemas.push_back(std::move(schema));

    std::string html = "<div class=\"review\"";
    if (!rating.empty()) html += " data-rating=\"" + escape_html(rating) + "\"";
    html += ">\n";
    if (!body_html.empty()) html += body_html + "\n";
    html += "</div>";
    return html;
}

// Dispatch a single schema block to its renderer. Returns the visible HTML
// (to substitute in place of the block) and appends any schema(s) produced.
std::string render_block(const std::string& type, const std::string& attrs,
                         const std::string& inner,
                         std::vector<nlohmann::json>& out_schemas) {
    if (type == "FAQPage") return render_faq(inner, out_schemas);
    if (type == "HowTo")   return render_howto(inner, out_schemas);
    if (type == "Review")  return render_review(attrs, inner, out_schemas);

    std::cerr << utils::warning_label() << " unknown schema type '" << type
              << "' — passing content through without a schema\n";
    return inner;
}

} // anonymous namespace

SchemaBlockProcessor::SchemaBlockProcessor() = default;

std::string SchemaBlockProcessor::process(const std::string& markdown,
                                          std::vector<nlohmann::json>& out_schemas,
                                          const nlohmann::json& page_context) const {
    (void)page_context;  // reserved for future use
    if (markdown.empty()) return markdown;

    static const std::regex open_re(R"RE(\{\%\s*schema\s*"([^"]+)"(.*?)\%\})RE");
    static const std::regex close_re(R"(\{\%\s*endschema\s*\%\})");

    std::string out;
    out.reserve(markdown.size());
    size_t pos = 0;
    while (pos < markdown.size()) {
        std::smatch om;
        if (!std::regex_search(markdown.cbegin() + pos, markdown.cend(), om, open_re)) {
            out.append(markdown, pos, std::string::npos);
            break;
        }
        size_t open_pos = pos + om.position();
        size_t open_end  = open_pos + om[0].length();
        std::string type  = om[1].str();
        std::string attrs = om[2].str();

        // Literal text preceding the block.
        out.append(markdown, pos, open_pos - pos);

        // Locate the matching close tag.
        std::smatch cm;
        if (!std::regex_search(markdown.cbegin() + open_end, markdown.cend(),
                               cm, close_re)) {
            // Unclosed block — leave the opener verbatim and continue.
            out += om[0].str();
            pos = open_end;
            continue;
        }
        size_t close_pos = open_end + cm.position();
        size_t close_end = close_pos + cm[0].length();
        std::string inner(markdown, open_end, close_pos - open_end);

        std::string html = render_block(type, attrs, inner, out_schemas);
        // Blank-line separation so cmark-gfm treats the emitted HTML as a
        // raw HTML block (CMARK_OPT_UNSAFE passes it through verbatim).
        out += "\n\n";
        out += html;
        out += "\n\n";
        pos = close_end;
    }
    return out;
}

std::vector<FaqPair> extract_faq_pairs(const std::string& text) {
    auto pairs = extract_marker_pairs(text, "##?");
    std::vector<FaqPair> out;
    out.reserve(pairs.size());
    for (auto& p : pairs) {
        FaqPair fp;
        fp.question = std::move(p.first);
        fp.answer_md = std::move(p.second);
        out.push_back(std::move(fp));
    }
    return out;
}

} // namespace cstatic
