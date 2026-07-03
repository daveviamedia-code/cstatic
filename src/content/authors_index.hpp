#pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace cstatic {

// E-E-A-T author entity system (G6). Author files live under
// cfg.authors_dir (default "src/authors") as `<slug>.md` with YAML
// frontmatter describing the person. The builder loads them into an
// AuthorsIndex once (Phase 1a), then:
//   - resolves page frontmatter `author: <slug>` to a full author object
//     exposed to templates as {{ page.author }}
//   - emits a Schema.org Person JSON-LD object for the page schema
//   - generates a per-author profile page at /<dir>/<slug>/
//
// Frontmatter fields: name, title, bio, avatar, email, twitter, linkedin,
// github, website, same_as (array), expertise (array).
class AuthorsIndex {
public:
    AuthorsIndex() = default;

    // Load all .md files from authors_dir. Each file's stem is the slug.
    // Missing/unreadable individual files emit a warning and are skipped.
    // A non-existent directory leaves the index empty (no warning).
    void load(const std::string& authors_dir);

    bool has(const std::string& slug) const;

    // Author data for template contexts: {slug, name, title, bio, avatar,
    // email, twitter, linkedin, github, website, same_as[], expertise[]}.
    // Returns null json when the slug is unknown.
    nlohmann::json context(const std::string& slug) const;

    // A Schema.org Person JSON-LD object: {@type, name, url, jobTitle,
    // email, image, sameAs}. `url` is the author's profile-page URL
    // (caller supplies base_url + path). Returns null json when unknown.
    nlohmann::json person_schema(const std::string& slug,
                                 const std::string& url) const;

    // All loaded slugs (sorted for deterministic output).
    std::vector<std::string> all_slugs() const;

    bool empty() const { return authors_.empty(); }
    size_t size() const { return authors_.size(); }

private:
    struct AuthorInfo {
        std::string slug;
        std::string name;
        std::string title;
        std::string bio;
        std::string avatar;
        std::string email;
        std::string twitter;
        std::string linkedin;
        std::string github;
        std::string website;
        std::vector<std::string> same_as;
        std::vector<std::string> expertise;
    };

    std::map<std::string, AuthorInfo> authors_;
};

} // namespace cstatic
