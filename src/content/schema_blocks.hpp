#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace cstatic {

// Processes `{% schema "Type" attrs %}...{% endschema %}` blocks in markdown.
// Each block emits BOTH visible HTML (in place of the tags, passed through
// cmark-gfm via CMARK_OPT_UNSAFE) and a JSON-LD schema object collected into
// `out_schemas`. The caller is expected to merge `out_schemas` into the page's
// `schema_extra` frontmatter so the seo_schema module (G3) emits them as
// additional <script type="application/ld+json"> blocks.
//
// Supported types:
//   "FAQPage"  — `##? question` headings; following paragraphs are the answer.
//                Visible: <section class="faq"><details><summary>…</summary>…
//                Schema:  FAQPage with Question/AcceptedAnswer pairs.
//   "HowTo"    — `##! step title` headings; following content is the step text.
//                Visible: <ol class="howto"><li><h3>…</h3>…</li>…
//                Schema:  HowTo with HowToStep entries.
//   "Review"   — `item="…" rating="N"` attrs; block body is the review text.
//                Visible: <div class="review" data-rating="N">…</div>
//                Schema:  Review with itemReviewed / reviewRating / reviewBody.
//
// Unknown types produce a one-line warning on stderr and pass through: the
// wrapper tags are stripped and the inner content is left for normal rendering
// (no schema emitted). Blocks do not nest.
//
// Usage:
//   SchemaBlockProcessor sbp;
//   std::vector<nlohmann::json> schemas;
//   body = sbp.process(body, schemas, page_context_json);
class SchemaBlockProcessor {
public:
    SchemaBlockProcessor();

    // Returns the markdown with every schema block replaced by its visible
    // HTML. Extracted JSON-LD objects are appended to `out_schemas` (each is a
    // complete schema object with @context + @type). `page_context` is
    // accepted for forward compatibility and currently unused.
    std::string process(const std::string& markdown,
                        std::vector<nlohmann::json>& out_schemas,
                        const nlohmann::json& page_context = {}) const;
};

// A question/answer pair extracted from text containing `##? question`
// headings. `answer_md` is the raw markdown between one question and the next
// (or end of input). Exposed as a standalone helper so G5 (FAQ extraction
// outside schema wrappers) can reuse the same parser.
struct FaqPair {
    std::string question;
    std::string answer_md;
};

// Parse `##? question` headings out of `text`. Headings must be at the start
// of a line (`##?` immediately followed by whitespace + question text). The
// answer is everything after the question line up to the next `##?` heading
// or the end of the text (trailing whitespace trimmed).
std::vector<FaqPair> extract_faq_pairs(const std::string& text);

// G5: process standalone `##?` FAQ pairs remaining in the body after schema
// blocks (G4) consumed their wrappers. Reuses extract_faq_pairs. `body` is
// returned with the FAQ region replaced by visible <details> HTML (content
// before the first `##?` line is preserved). `found` is false when the body
// has no standalone `##?` questions (body returned unchanged).
struct StandaloneFaqResult {
    std::string body;                        // body with FAQ region -> visible <details>
    nlohmann::json faq_ctx;                  // [{question, answer_html, answer_text}]
    std::vector<nlohmann::json> questions;   // Question schema objects (for FAQPage)
    bool found = false;
};
StandaloneFaqResult process_standalone_faq(const std::string& body);

// Merge Question schema objects into a FAQPage inside `schema_extra` (a JSON
// array): appends to an existing FAQPage's mainEntity, else pushes a new
// FAQPage entry. Lets G5 combine standalone ##? pairs with a G4 {% schema
// "FAQPage" %} block on the same page into one FAQPage.
void merge_faq_into_schema_extra(nlohmann::json& schema_extra,
                                 const std::vector<nlohmann::json>& questions);

} // namespace cstatic
