#include "content/authors_index.hpp"

#include "content/frontmatter.hpp"
#include "utils/file_io.hpp"
#include "utils/terminal.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>

namespace cstatic {

namespace fs = std::filesystem;

namespace {

// Read a string field from the parsed frontmatter (known field or custom).
std::string get_str(const Frontmatter& fm, const std::string& key) {
    if (key == "title")      return fm.title;
    if (key == "description") return fm.description;
    if (fm.custom.contains(key) && fm.custom[key].is_string()) {
        return fm.custom[key].get<std::string>();
    }
    return {};
}

// Read a string-array field from custom frontmatter.
std::vector<std::string> get_str_array(const Frontmatter& fm, const std::string& key) {
    std::vector<std::string> out;
    if (fm.custom.contains(key) && fm.custom[key].is_array()) {
        for (const auto& v : fm.custom[key]) {
            if (v.is_string()) out.push_back(v.get<std::string>());
        }
    }
    return out;
}

} // namespace

void AuthorsIndex::load(const std::string& authors_dir) {
    if (authors_dir.empty() || !fs::exists(authors_dir) || !fs::is_directory(authors_dir)) {
        return;
    }

    for (const auto& entry : fs::recursive_directory_iterator(authors_dir)) {
        if (!entry.is_regular_file()) continue;
        const auto& path = entry.path();
        if (path.extension() != ".md") continue;

        std::string slug = path.stem().string();
        std::string content;
        try {
            content = utils::read_file(path.string());
        } catch (const std::exception& e) {
            std::cerr << utils::warning_label() << " author file '" << path.string()
                      << "' could not be read: " << e.what() << "\n";
            continue;
        }

        ParsedContent parsed;
        try {
            parsed = parse_frontmatter(content, path.string());
        } catch (const FrontmatterError& e) {
            std::cerr << utils::warning_label() << " author file '" << path.string()
                      << "': " << e.what() << "\n";
            continue;
        }

        AuthorInfo info;
        info.slug      = slug;
        info.name      = get_str(parsed.frontmatter, "name");
        info.title     = get_str(parsed.frontmatter, "title");
        info.bio       = get_str(parsed.frontmatter, "bio");
        info.avatar    = get_str(parsed.frontmatter, "avatar");
        info.email     = get_str(parsed.frontmatter, "email");
        info.twitter   = get_str(parsed.frontmatter, "twitter");
        info.linkedin  = get_str(parsed.frontmatter, "linkedin");
        info.github    = get_str(parsed.frontmatter, "github");
        info.website   = get_str(parsed.frontmatter, "website");
        info.same_as   = get_str_array(parsed.frontmatter, "same_as");
        info.expertise = get_str_array(parsed.frontmatter, "expertise");

        // Fall back to the slug as the display name if frontmatter omits one.
        if (info.name.empty()) info.name = slug;

        authors_[slug] = std::move(info);
    }
}

bool AuthorsIndex::has(const std::string& slug) const {
    return authors_.find(slug) != authors_.end();
}

nlohmann::json AuthorsIndex::context(const std::string& slug) const {
    auto it = authors_.find(slug);
    if (it == authors_.end()) return nullptr;

    const auto& a = it->second;
    nlohmann::json j;
    j["slug"]      = a.slug;
    j["name"]      = a.name;
    if (!a.title.empty())     j["title"]     = a.title;
    if (!a.bio.empty())       j["bio"]       = a.bio;
    if (!a.avatar.empty())    j["avatar"]    = a.avatar;
    if (!a.email.empty())     j["email"]     = a.email;
    if (!a.twitter.empty())   j["twitter"]   = a.twitter;
    if (!a.linkedin.empty())  j["linkedin"]  = a.linkedin;
    if (!a.github.empty())    j["github"]    = a.github;
    if (!a.website.empty())   j["website"]   = a.website;
    if (!a.same_as.empty()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& s : a.same_as) arr.push_back(s);
        j["same_as"] = arr;
    }
    if (!a.expertise.empty()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& s : a.expertise) arr.push_back(s);
        j["expertise"] = arr;
    }
    return j;
}

nlohmann::json AuthorsIndex::person_schema(const std::string& slug,
                                           const std::string& url) const {
    auto it = authors_.find(slug);
    if (it == authors_.end()) return nullptr;

    const auto& a = it->second;
    nlohmann::json p;
    p["@type"] = "Person";
    p["name"]  = a.name;
    if (!url.empty()) p["url"] = url;
    if (!a.title.empty())  p["jobTitle"] = a.title;
    if (!a.email.empty())  p["email"]    = a.email;
    if (!a.avatar.empty()) p["image"]    = a.avatar;
    if (!a.website.empty()) {
        p["@id"] = a.website;
    }
    if (!a.same_as.empty()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& s : a.same_as) arr.push_back(s);
        p["sameAs"] = arr;
    }
    return p;
}

std::vector<std::string> AuthorsIndex::all_slugs() const {
    std::vector<std::string> slugs;
    slugs.reserve(authors_.size());
    for (const auto& [slug, _] : authors_) {
        slugs.push_back(slug);
    }
    return slugs;  // already sorted (std::map key order)
}

} // namespace cstatic
