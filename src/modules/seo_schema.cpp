#include "modules/seo_schema.hpp"
#include "config/config.hpp"
#include "utils/terminal.hpp"
#include "utils/file_io.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace cstatic {
namespace modules {
namespace seo_schema {

namespace fs = std::filesystem;

namespace {

// Wrap a JSON object in a JSON-LD <script> block (pretty-printed, 2-space).
std::string make_script(const nlohmann::json& j) {
    return "<script type=\"application/ld+json\">\n" + j.dump(2) + "\n</script>\n";
}

// Join a base URL and a path into an absolute URL. Absolute URLs (with
// scheme) are returned unchanged; paths starting with '/' replace the path
// root of `base`; relative paths are appended after a '/' separator.
std::string resolve_url(const std::string& base, const std::string& path) {
    if (path.empty()) return "";
    if (path.find("://") != std::string::npos) return path;  // already absolute
    if (path.front() == '/') return base + path;
    return base + "/" + path;
}

// True if the page lives under /posts/ (the default collection). Used to pick
// BlogPosting as the default @type when no explicit type is set.
bool is_post(const nlohmann::json& page) {
    std::string url = page.value("url", "");
    return url.rfind("/posts/", 0) == 0;
}

// Pick a comma-joined keywords string or array from the page: explicit
// `keywords` wins; otherwise fall back to comma-joined `tags` when allowed.
// Returns null when nothing suitable is present (caller omits the field).
nlohmann::json resolve_keywords(const nlohmann::json& page, bool fallback_to_tags) {
    if (page.contains("keywords") && !page["keywords"].is_null()) {
        return page["keywords"];
    }
    if (fallback_to_tags && page.contains("tags") && page["tags"].is_array()
        && !page["tags"].empty()) {
        std::string joined;
        for (const auto& t : page["tags"]) {
            if (!t.is_string()) continue;
            if (!joined.empty()) joined += ", ";
            joined += t.get<std::string>();
        }
        return joined;
    }
    return nullptr;
}

// Recursively merge `overlay` into `base`: objects recurse, everything else
// (scalars, arrays) replaces. Used so an explicit `page.schema` block can
// override individual fields without blowing away the auto-generated ones.
void deep_merge(nlohmann::json& base, const nlohmann::json& overlay) {
    if (!base.is_object() || !overlay.is_object()) {
        base = overlay;
        return;
    }
    for (auto it = overlay.begin(); it != overlay.end(); ++it) {
        if (base.contains(it.key()) && base[it.key()].is_object() && it.value().is_object()) {
            deep_merge(base[it.key()], it.value());
        } else {
            base[it.key()] = it.value();
        }
    }
}

// Resolve the page description using the G9 preference chain:
// tldr → description → excerpt. Returns empty string when none are present.
std::string resolve_description(const nlohmann::json& page) {
    std::string desc = page.value("tldr", "");
    if (desc.empty()) desc = page.value("description", "");
    if (desc.empty()) desc = page.value("excerpt", "");
    return desc;
}

// --- Site-wide schemas ---

nlohmann::json build_website_schema(const Config& cfg) {
    nlohmann::json j;
    j["@context"] = "https://schema.org";
    j["@type"] = "WebSite";
    j["name"] = cfg.site_title;
    j["url"] = cfg.org_url.empty() ? cfg.site_base_url : cfg.org_url;
    if (!cfg.website_search_url_template.empty()) {
        nlohmann::json pa;
        pa["@type"] = "SearchAction";
        pa["target"] = resolve_url(cfg.site_base_url, cfg.website_search_url_template);
        pa["query-input"] = "required name=search_term_string";
        j["potentialAction"] = pa;
    }
    return j;
}

nlohmann::json build_organization_schema(const Config& cfg) {
    nlohmann::json j;
    j["@context"] = "https://schema.org";
    j["@type"] = "Organization";
    j["name"] = cfg.org_name;
    j["url"] = cfg.org_url.empty() ? cfg.site_base_url : cfg.org_url;
    if (!cfg.org_legal_name.empty()) j["legalName"] = cfg.org_legal_name;
    if (!cfg.org_logo.empty()) j["logo"] = resolve_url(cfg.site_base_url, cfg.org_logo);
    if (!cfg.org_founding_date.empty()) j["foundingDate"] = cfg.org_founding_date;
    if (!cfg.org_founders.empty()) {
        nlohmann::json founders = nlohmann::json::array();
        for (const auto& f : cfg.org_founders) {
            nlohmann::json p;
            p["@type"] = "Person";
            p["name"] = f;
            founders.push_back(p);
        }
        j["founder"] = founders;
    }
    if (!cfg.org_same_as.empty()) {
        nlohmann::json same_as = nlohmann::json::array();
        for (const auto& s : cfg.org_same_as) same_as.push_back(s);
        j["sameAs"] = same_as;
    }
    return j;
}

// --- Page-level schemas ---

nlohmann::json build_webpage_schema(const Config& cfg, const nlohmann::json& page,
                                    const std::string& type) {
    nlohmann::json j;
    j["@context"] = "https://schema.org";
    j["@type"] = type;

    std::string title = page.value("title", "");
    if (!title.empty()) j["name"] = title;

    std::string canonical = page.value("canonical", "");
    std::string url = !canonical.empty() ? canonical
                      : resolve_url(cfg.site_base_url, page.value("url", ""));
    if (!url.empty()) j["url"] = url;

    std::string desc = resolve_description(page);
    if (!desc.empty()) j["description"] = desc;

    std::string image = page.value("image", "");
    if (!image.empty()) j["image"] = resolve_url(cfg.site_base_url, image);

    nlohmann::json part_of;
    part_of["@type"] = "WebSite";
    part_of["name"] = cfg.site_title;
    part_of["url"] = cfg.org_url.empty() ? cfg.site_base_url : cfg.org_url;
    j["isPartOf"] = part_of;

    std::string date = page.value("date", "");
    if (!date.empty()) j["dateModified"] = date;

    nlohmann::json kw = resolve_keywords(page, /*fallback_to_tags=*/false);
    if (!kw.is_null()) j["keywords"] = kw;

    return j;
}

nlohmann::json build_article_schema(const Config& cfg, const nlohmann::json& page,
                                    const std::string& type) {
    nlohmann::json j;
    j["@context"] = "https://schema.org";
    j["@type"] = type;

    std::string title = page.value("title", "");
    if (!title.empty()) j["headline"] = title;

    std::string date = page.value("date", "");
    if (!date.empty()) {
        j["datePublished"] = date;
        j["dateModified"] = date;
    }

    if (page.contains("author")) {
        const auto& av = page["author"];
        if (av.is_string()) {
            std::string author = av.get<std::string>();
            if (!author.empty()) {
                nlohmann::json a;
                a["@type"] = "Person";
                a["name"] = author;
                j["author"] = a;
            }
        } else if (av.is_object()) {
            // Already a resolved Person schema (from the authors index, G6).
            j["author"] = av;
        }
    }

    std::string image = page.value("image", "");
    if (!image.empty()) j["image"] = resolve_url(cfg.site_base_url, image);

    // Publisher: full Organization when org_name is set, else a minimal one
    // using the site title (articles require a publisher per Schema.org).
    nlohmann::json pub;
    pub["@type"] = "Organization";
    if (!cfg.org_name.empty()) {
        pub["name"] = cfg.org_name;
        if (!cfg.org_logo.empty()) {
            pub["logo"] = resolve_url(cfg.site_base_url, cfg.org_logo);
        }
    } else {
        pub["name"] = cfg.site_title;
    }
    j["publisher"] = pub;

    std::string canonical = page.value("canonical", "");
    std::string url = !canonical.empty() ? canonical
                      : resolve_url(cfg.site_base_url, page.value("url", ""));
    if (!url.empty()) {
        nlohmann::json meop;
        meop["@type"] = "WebPage";
        meop["@id"] = url;
        j["mainEntityOfPage"] = meop;
    }

    std::string desc = resolve_description(page);
    if (!desc.empty()) j["description"] = desc;

    nlohmann::json kw = resolve_keywords(page, /*fallback_to_tags=*/true);
    if (!kw.is_null()) j["keywords"] = kw;

    std::string excerpt = page.value("excerpt", "");
    if (!excerpt.empty()) j["articleBody"] = excerpt;

    return j;
}

nlohmann::json build_product_schema(const Config& cfg, const nlohmann::json& page) {
    nlohmann::json j;
    j["@context"] = "https://schema.org";
    j["@type"] = "Product";

    std::string title = page.value("title", "");
    if (!title.empty()) j["name"] = title;

    std::string desc = resolve_description(page);
    if (!desc.empty()) j["description"] = desc;

    std::string image = page.value("image", "");
    if (!image.empty()) j["image"] = resolve_url(cfg.site_base_url, image);

    std::string brand = page.value("brand", "");
    if (!brand.empty()) {
        nlohmann::json b;
        b["@type"] = "Brand";
        b["name"] = brand;
        j["brand"] = b;
    }

    if (page.contains("price") && !page["price"].is_null()) {
        nlohmann::json offer;
        offer["@type"] = "Offer";
        offer["price"] = page["price"];
        if (page.contains("currency") && page["currency"].is_string()) {
            offer["priceCurrency"] = page["currency"].get<std::string>();
        }
        if (page.contains("availability") && page["availability"].is_string()) {
            offer["availability"] = page["availability"].get<std::string>();
        }
        j["offers"] = offer;
    }

    if (page.contains("rating") && !page["rating"].is_null()) {
        nlohmann::json ar;
        ar["@type"] = "AggregateRating";
        ar["ratingValue"] = page["rating"];
        if (page.contains("reviewCount") && !page["reviewCount"].is_null()) {
            ar["reviewCount"] = page["reviewCount"];
        }
        j["aggregateRating"] = ar;
    }

    return j;
}

nlohmann::json build_software_application_schema(const Config& cfg,
                                                 const nlohmann::json& page) {
    nlohmann::json j;
    j["@context"] = "https://schema.org";
    j["@type"] = "SoftwareApplication";

    std::string title = page.value("title", "");
    if (!title.empty()) j["name"] = title;

    std::string desc = resolve_description(page);
    if (!desc.empty()) j["description"] = desc;

    std::string category = page.value("application_category", "");
    if (category.empty()) category = page.value("category", "");
    if (!category.empty()) j["applicationCategory"] = category;

    std::string os = page.value("operating_system", "");
    if (!os.empty()) j["operatingSystem"] = os;

    std::string image = page.value("image", "");
    if (!image.empty()) j["image"] = resolve_url(cfg.site_base_url, image);

    if (page.contains("price") && !page["price"].is_null()) {
        nlohmann::json offer;
        offer["@type"] = "Offer";
        offer["price"] = page["price"];
        if (page.contains("currency") && page["currency"].is_string()) {
            offer["priceCurrency"] = page["currency"].get<std::string>();
        }
        if (page.contains("availability") && page["availability"].is_string()) {
            offer["availability"] = page["availability"].get<std::string>();
        }
        j["offers"] = offer;
    }

    if (page.contains("rating") && !page["rating"].is_null()) {
        nlohmann::json ar;
        ar["@type"] = "AggregateRating";
        ar["ratingValue"] = page["rating"];
        if (page.contains("reviewCount") && !page["reviewCount"].is_null()) {
            ar["reviewCount"] = page["reviewCount"];
        }
        j["aggregateRating"] = ar;
    }

    return j;
}

// G8: inject passage index as `hasPart` on the page schema. WebPageElement
// entries carry the slugified id (as an in-page anchor URL), the heading
// text, and the body excerpt. Called from build_page_schema AFTER the
// type-specific builder, BEFORE deep_merge — so an explicit `page.schema.hasPart`
// in frontmatter still wins.
void add_has_part(nlohmann::json& schema, const Config& cfg,
                  const nlohmann::json& page) {
    if (!page.contains("passages") || !page["passages"].is_array()
        || page["passages"].empty()) {
        return;
    }
    std::string canonical = page.value("canonical", "");
    std::string base = canonical.empty()
                       ? resolve_url(cfg.site_base_url, page.value("url", ""))
                       : canonical;
    nlohmann::json has_part = nlohmann::json::array();
    for (const auto& p : page["passages"]) {
        if (!p.is_object()) continue;
        nlohmann::json elem;
        elem["@type"] = "WebPageElement";
        if (p.contains("heading")) elem["name"] = p["heading"];
        if (p.contains("text"))    elem["text"] = p["text"];
        if (!base.empty() && p.contains("id") && p["id"].is_string()) {
            elem["url"] = base + "#" + p["id"].get<std::string>();
        }
        has_part.push_back(std::move(elem));
    }
    if (!has_part.empty()) {
        schema["hasPart"] = std::move(has_part);
    }
}

// G9: inject key_takeaways as an ItemList on the page schema's mainEntity.
// Called from build_page_schema AFTER the type-specific builder + add_has_part,
// BEFORE deep_merge — so an explicit `page.schema.mainEntity` still wins.
void add_key_takeaways(nlohmann::json& schema, const nlohmann::json& page) {
    if (!page.contains("key_takeaways") || !page["key_takeaways"].is_array()
        || page["key_takeaways"].empty()) {
        return;
    }
    nlohmann::json items = nlohmann::json::array();
    int position = 0;
    for (const auto& k : page["key_takeaways"]) {
        if (!k.is_string()) continue;
        const std::string& text = k.get<std::string>();
        if (text.empty()) continue;
        ++position;
        nlohmann::json item;
        item["@type"] = "ListItem";
        item["position"] = position;
        item["name"] = text;
        items.push_back(std::move(item));
    }
    if (!items.empty()) {
        nlohmann::json me;
        me["@type"] = "ItemList";
        me["itemListElement"] = std::move(items);
        schema["mainEntity"] = std::move(me);
    }
}

// G12: inject readability metrics onto the page schema. wordCount is
// Schema.org-valid only on Article-family types, so we gate it on @type;
// timeRequired applies to any page that has a reading time. Called from
// build_page_schema AFTER add_key_takeaways, BEFORE deep_merge so an
// explicit `page.schema.wordCount`/`timeRequired` still wins.
void add_readability(nlohmann::json& schema, const nlohmann::json& page) {
    bool has_word_count = page.contains("word_count") &&
                          page["word_count"].is_number_integer();
    bool has_reading_time = page.contains("reading_time") &&
                            page["reading_time"].is_number_integer();

    if (has_word_count && page["word_count"].get<int>() > 0) {
        std::string type = schema.value("@type", "");
        if (type == "BlogPosting" || type == "Article" ||
            type == "NewsArticle" || type == "TechArticle") {
            schema["wordCount"] = page["word_count"].get<int>();
        }
    }

    if (has_reading_time && page["reading_time"].get<int>() > 0) {
        // ISO 8601 duration: PT5M = 5 minutes.
        schema["timeRequired"] = "PT" +
            std::to_string(page["reading_time"].get<int>()) + "M";
    }
}

// Resolve the @type using the documented precedence, build the auto schema,
// then deep-merge any explicit `page.schema` over it.
nlohmann::json build_page_schema(const Config& cfg, const nlohmann::json& page) {
    bool has_schema = page.contains("schema") && page["schema"].is_object();

    std::string type = "WebPage";
    if (has_schema && page["schema"].contains("@type")
        && page["schema"]["@type"].is_string()) {
        type = page["schema"]["@type"].get<std::string>();
    } else if (page.contains("type") && page["type"].is_string()
               && !page["type"].get<std::string>().empty()) {
        type = page["type"].get<std::string>();
    } else if (is_post(page)) {
        type = "BlogPosting";
    }

    nlohmann::json schema;
    if (type == "Product") {
        schema = build_product_schema(cfg, page);
    } else if (type == "SoftwareApplication") {
        schema = build_software_application_schema(cfg, page);
    } else if (type == "BlogPosting" || type == "Article"
               || type == "NewsArticle" || type == "TechArticle") {
        schema = build_article_schema(cfg, page, type);
    } else {
        schema = build_webpage_schema(cfg, page, type);
    }

    // G8: attach passage index (before deep_merge so explicit schema wins).
    add_has_part(schema, cfg, page);

    // G9: attach key takeaways as mainEntity ItemList (before deep_merge).
    add_key_takeaways(schema, page);

    // G12: attach wordCount (Article types) + timeRequired (any page).
    add_readability(schema, page);

    if (has_schema) {
        deep_merge(schema, page["schema"]);
    }

    return schema;
}

// Build a BreadcrumbList walking the URL ancestors. Returns null when the
// page is root-level (no breadcrumbs to show).
nlohmann::json build_breadcrumb(const Config& cfg, const nlohmann::json& page,
                                const nlohmann::json& pages) {
    std::string url = page.value("url", "");
    if (url.empty() || url == "/") return nullptr;

    // Split into cumulative prefix URLs: "/posts/hello/" →
    // ["/", "/posts/", "/posts/hello/"].
    std::vector<std::string> prefixes;
    prefixes.push_back("/");
    {
        size_t pos = 1;  // skip leading '/'
        while (pos < url.size()) {
            size_t next = url.find('/', pos);
            if (next == std::string::npos) break;
            prefixes.push_back(url.substr(0, next + 1));
            pos = next + 1;
        }
    }
    if (prefixes.back() != url) prefixes.push_back(url);

    nlohmann::json items = nlohmann::json::array();
    int position = 0;
    for (const auto& prefix : prefixes) {
        ++position;
        std::string name;
        if (prefix == "/") {
            name = cfg.site_title;
        } else if (prefix == url) {
            name = page.value("title", "");
        } else {
            for (const auto& p : pages) {
                if (p.value("url", "") == prefix) {
                    name = p.value("title", "");
                    break;
                }
            }
        }
        if (name.empty()) {
            // Derive a human-readable name from the final URL segment.
            std::string seg = prefix;
            if (!seg.empty() && seg.back() == '/') seg.pop_back();
            size_t slash = seg.rfind('/');
            if (slash != std::string::npos) seg = seg.substr(slash + 1);
            for (char& c : seg) {
                if (c == '-' || c == '_') c = ' ';
            }
            if (!seg.empty()) {
                seg[0] = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(seg[0])));
            }
            name = seg;
        }

        nlohmann::json item;
        item["@type"] = "ListItem";
        item["position"] = position;
        item["name"] = name;
        item["item"] = resolve_url(cfg.site_base_url, prefix);
        items.push_back(item);
    }

    nlohmann::json bl;
    bl["@context"] = "https://schema.org";
    bl["@type"] = "BreadcrumbList";
    bl["itemListElement"] = items;
    return bl;
}

} // anonymous namespace

std::string build_website_script(const Config& cfg) {
    return make_script(build_website_schema(cfg));
}

std::string build_organization_script(const Config& cfg) {
    if (cfg.org_name.empty()) return "";
    return make_script(build_organization_schema(cfg));
}

std::vector<SchemaIssue> validate(const nlohmann::json& s,
                                  const std::string& page_url) {
    std::vector<SchemaIssue> issues;
    if (!s.is_object()) return issues;
    std::string type = s.value("@type", "");

    // Walk a dotted path; return true only if the value is present and
    // non-empty (strings non-empty, not null).
    auto has_nonempty = [&](const std::string& dotted) -> bool {
        const nlohmann::json* cur = &s;
        std::stringstream ss(dotted);
        std::string key;
        while (std::getline(ss, key, '.')) {
            if (!cur->is_object() || !cur->contains(key)) return false;
            cur = &(*cur)[key];
        }
        if (cur->is_null()) return false;
        if (cur->is_string()) return !cur->get<std::string>().empty();
        return true;
    };

    auto require = [&](const std::string& field) {
        if (!has_nonempty(field)) {
            issues.push_back({page_url, type, field, "missing or empty required field"});
        }
    };

    if (type == "WebPage") {
        require("name");
    } else if (type == "BlogPosting" || type == "Article" || type == "NewsArticle") {
        require("headline");
        require("datePublished");
        require("author");
    } else if (type == "TechArticle") {
        require("headline");
        require("datePublished");
    } else if (type == "Product") {
        require("name");
        require("offers.price");
    } else if (type == "SoftwareApplication") {
        require("name");
        require("applicationCategory");
    }
    // Unknown types: skip.

    return issues;
}

std::string build_json_ld(const Config& cfg, const nlohmann::json& page,
                          const nlohmann::json& pages) {
    if (!cfg.json_ld_enabled) return "";

    std::string out;

    // 1. Site-wide WebSite schema (always).
    out += make_script(build_website_schema(cfg));

    // 2. Organization schema when configured.
    if (!cfg.org_name.empty()) {
        out += make_script(build_organization_schema(cfg));
    }

    // 3. Page-level schema (+ validation warnings to stderr).
    nlohmann::json page_schema = build_page_schema(cfg, page);
    std::string page_url = page.value("url", "");
    for (const auto& issue : validate(page_schema, page_url)) {
        std::cerr << utils::warning_label() << " json-ld [" << issue.schema_type
                  << "] " << issue.page_url << ": '" << issue.field << "' — "
                  << issue.message << "\n";
    }
    out += make_script(page_schema);

    // 4. BreadcrumbList for nested pages (includes the current page as the
    //    last item per Schema.org spec).
    if (!page_url.empty() && page_url != "/") {
        nlohmann::json bl = build_breadcrumb(cfg, page, pages);
        if (!bl.is_null()) out += make_script(bl);
    }

    // 5. Each schema_extra entry verbatim (array of objects, or a single object).
    if (page.contains("schema_extra") && !page["schema_extra"].is_null()) {
        const auto& extra = page["schema_extra"];
        if (extra.is_array()) {
            for (const auto& e : extra) {
                if (e.is_object()) out += make_script(e);
            }
        } else if (extra.is_object()) {
            out += make_script(extra);
        }
    }

    return out;
}

std::string build_citation_tags(const Config& cfg, const nlohmann::json& page) {
    if (!cfg.citation_tags_enabled) return "";

    std::ostringstream out;

    auto emit = [&](const std::string& name, const std::string& content) {
        if (!content.empty()) {
            out << "<meta name=\"" << name << "\" content=\""
                << utils::xml_escape(content) << "\">\n";
        }
    };

    // citation_author — one per author.
    // author may be a raw string slug, a resolved Person object (G6), or
    // an entry in an `authors` array.
    auto emit_author = [&](const nlohmann::json& a) {
        if (a.is_string()) {
            emit("citation_author", a.get<std::string>());
        } else if (a.is_object() && a.contains("name") && a["name"].is_string()) {
            emit("citation_author", a["name"].get<std::string>());
        }
    };

    if (page.contains("author")) {
        emit_author(page["author"]);
    }
    if (page.contains("authors") && page["authors"].is_array()) {
        for (const auto& a : page["authors"]) {
            emit_author(a);
        }
    }

    emit("citation_title", page.value("title", ""));
    emit("citation_publication_date", page.value("date", ""));

    // citation_online_date — created falls back to date.
    {
        std::string online = page.value("created", "");
        if (online.empty()) online = page.value("date", "");
        emit("citation_online_date", online);
    }

    emit("citation_pdf_url", page.value("pdf_url", ""));

    // citation_abstract — tldr (G9) preferred, falls back to description.
    {
        std::string abstract = page.value("tldr", "");
        if (abstract.empty()) abstract = page.value("description", "");
        emit("citation_abstract", abstract);
    }

    emit("citation_journal_title", page.value("journal", ""));
    emit("citation_doi", page.value("doi", ""));

    // citation_keywords — semicolon-joined tags.
    if (page.contains("tags") && page["tags"].is_array() && !page["tags"].empty()) {
        std::string keywords;
        for (const auto& t : page["tags"]) {
            if (!t.is_string()) continue;
            if (!keywords.empty()) keywords += "; ";
            keywords += t.get<std::string>();
        }
        emit("citation_keywords", keywords);
    }

    return out.str();
}

std::vector<OrgIssue> validate_organization(const Config& cfg,
                                             const std::vector<std::string>& known_author_slugs) {
    std::vector<OrgIssue> issues;
    if (cfg.org_name.empty()) return issues;

    // 1. org_name vs site_title — informational (intentional sometimes).
    if (!cfg.site_title.empty() && cfg.org_name != cfg.site_title) {
        issues.push_back({"org_name",
            "'" + cfg.org_name + "' differs from site_title ('" + cfg.site_title +
            "') — intentional if the organization and site have different names"});
    }

    // 2. org_logo — file-existence check when it's a local path (not a URL).
    if (!cfg.org_logo.empty() && cfg.org_logo.find("://") == std::string::npos) {
        std::string rel = cfg.org_logo;
        if (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
        std::string logo_path = cfg.static_dir + "/" + rel;
        if (!fs::exists(logo_path)) {
            issues.push_back({"org_logo",
                "file not found at '" + logo_path + "' (resolved relative to static_dir)"});
        }
    }

    // 3. same_as entries — each should be an http(s) URL.
    for (size_t i = 0; i < cfg.org_same_as.size(); ++i) {
        const std::string& s = cfg.org_same_as[i];
        if (s.find("://") == std::string::npos) {
            issues.push_back({"org_same_as[" + std::to_string(i) + "]",
                "'" + s + "' is not a valid URL (expected http:// or https://)"});
        }
    }

    // 4. org_founders — warn when a founder doesn't match a known author slug.
    //    Only checked when the authors index is enabled and non-empty.
    if (!known_author_slugs.empty()) {
        for (const auto& f : cfg.org_founders) {
            if (std::find(known_author_slugs.begin(), known_author_slugs.end(), f)
                == known_author_slugs.end()) {
                issues.push_back({"org_founders",
                    "'" + f + "' does not match a known author slug"});
            }
        }
    }

    return issues;
}

nlohmann::json build_org_context(const Config& cfg) {
    nlohmann::json org = nlohmann::json::object();
    if (cfg.org_name.empty()) return org;

    org["name"] = cfg.org_name;
    org["url"] = cfg.org_url.empty() ? cfg.site_base_url : cfg.org_url;
    if (!cfg.org_legal_name.empty()) org["legal_name"] = cfg.org_legal_name;
    if (!cfg.org_logo.empty()) org["logo_url"] = resolve_url(cfg.site_base_url, cfg.org_logo);
    if (!cfg.org_founding_date.empty()) org["founding_date"] = cfg.org_founding_date;
    if (!cfg.org_founders.empty()) {
        nlohmann::json founders = nlohmann::json::array();
        for (const auto& f : cfg.org_founders) founders.push_back(f);
        org["founders"] = std::move(founders);
    }
    if (!cfg.org_same_as.empty()) {
        nlohmann::json same_as = nlohmann::json::array();
        for (const auto& s : cfg.org_same_as) same_as.push_back(s);
        org["same_as"] = std::move(same_as);
    }
    return org;
}

} // namespace seo_schema
} // namespace modules
} // namespace cstatic
