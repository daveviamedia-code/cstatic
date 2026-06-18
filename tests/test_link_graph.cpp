#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "content/link_graph.hpp"

using namespace cstatic;

// ---------------------------------------------------------------------------
// Note: LinkGraph is purely in-memory — no fixture or temp directory is
// needed. Each TEST_CASE builds its own graph from scratch.
// ---------------------------------------------------------------------------

TEST_CASE("LinkGraph: rewrites wikilink by filename stem", "[link_graph]") {
    LinkGraph lg;
    lg.index_page("/posts/hello/", "Hello World");

    std::string md = "See [[hello]] for more.";
    auto encountered = lg.rewrite_wikilinks(md, "/a/");
    REQUIRE(md == "See <a href=\"/posts/hello/\">hello</a> for more.");
    REQUIRE(encountered.size() == 1);
    REQUIRE(encountered[0].target == "hello");
    REQUIRE(encountered[0].display == "hello");
    REQUIRE(encountered[0].resolved_url == "/posts/hello/");
}

TEST_CASE("LinkGraph: rewrites wikilink by lowercase title", "[link_graph]") {
    LinkGraph lg;
    lg.index_page("/posts/hello/", "Hello World");

    // target has spaces — slugify matches the slug index, but also exercise
    // the lowercase-title branch by indexing a page whose title slug differs.
    std::string md = "Read [[Hello World]] today.";
    lg.rewrite_wikilinks(md, "/a/");
    REQUIRE(md.find("href=\"/posts/hello/\"") != std::string::npos);
    REQUIRE(md.find(">Hello World<") != std::string::npos);
}

TEST_CASE("LinkGraph: rewrites wikilink by title casing only", "[link_graph]") {
    // Page with a title whose slug doesn't equal the filename stem.
    LinkGraph lg;
    lg.index_page("/about/", "About Me");

    std::string md = "[[about me]] is the about page.";
    lg.rewrite_wikilinks(md, "/a/");
    REQUIRE(md.find("href=\"/about/\"") != std::string::npos);
    REQUIRE(md.find(">about me<") != std::string::npos);
}

TEST_CASE("LinkGraph: pipe syntax overrides display", "[link_graph]") {
    LinkGraph lg;
    lg.index_page("/hello/", "Hello World");

    std::string md = "[[hello|custom display]] here.";
    auto encountered = lg.rewrite_wikilinks(md, "/a/");
    REQUIRE(encountered.size() == 1);
    REQUIRE(encountered[0].target == "hello");
    REQUIRE(encountered[0].display == "custom display");
    REQUIRE(encountered[0].resolved_url == "/hello/");
    REQUIRE(md.find(">custom display<") != std::string::npos);
}

TEST_CASE("LinkGraph: alias resolves wikilink", "[link_graph]") {
    LinkGraph lg;
    lg.index_page("/new-path/", "New Title", {"/old-path/", "legacy-name"});

    for (const std::string& target : {"/old-path/", "legacy-name"}) {
        std::string md = "[[" + target + "]]";
        lg.rewrite_wikilinks(md, "/a/");
        REQUIRE(md.find("href=\"/new-path/\"") != std::string::npos);
    }
}

TEST_CASE("LinkGraph: unresolved wikilink renders class without href", "[link_graph]") {
    LinkGraph lg;
    lg.index_page("/hello/", "Hello");

    std::string md = "See [[nonexistent]] please.";
    auto encountered = lg.rewrite_wikilinks(md, "/a/");
    REQUIRE(encountered.size() == 1);
    REQUIRE(encountered[0].resolved_url.empty());
    // No href=, class is set, display preserved.
    REQUIRE(md.find("href=") == std::string::npos);
    REQUIRE(md.find("class=\"wikilink-unresolved\"") != std::string::npos);
    REQUIRE(md.find(">nonexistent<") != std::string::npos);
}

TEST_CASE("LinkGraph: empty display falls back to target", "[link_graph]") {
    LinkGraph lg;
    lg.index_page("/hello/", "Hello");

    // Pipe with only whitespace on the right — display falls back to target.
    std::string md = "[[hello|   ]]";
    auto encountered = lg.rewrite_wikilinks(md, "/a/");
    REQUIRE(encountered[0].display == "hello");
    REQUIRE(md.find(">hello<") != std::string::npos);
}

TEST_CASE("LinkGraph: multiple wikilinks all rewrite", "[link_graph]") {
    LinkGraph lg;
    lg.index_page("/a/", "Alpha");
    lg.index_page("/b/", "Beta");

    std::string md = "[[a]] and [[b]] plus [[a|first]] in one line.";
    auto encountered = lg.rewrite_wikilinks(md, "/src/");
    REQUIRE(encountered.size() == 3);
    REQUIRE(md.find("href=\"/a/\">a</a>") != std::string::npos);
    REQUIRE(md.find("href=\"/b/\">b</a>") != std::string::npos);
    REQUIRE(md.find("href=\"/a/\">first</a>") != std::string::npos);
}

TEST_CASE("LinkGraph: no false matches on bracket-like text", "[link_graph]") {
    LinkGraph lg;
    lg.index_page("/a/", "Alpha");

    // Single bracket, dangling open, dangling close — none should match.
    std::string md = "[single] and [[[] and ]] and [a]";
    lg.rewrite_wikilinks(md, "/src/");
    REQUIRE(md.find("href=") == std::string::npos);
    REQUIRE(md.find("wikilink-unresolved") == std::string::npos);
}

TEST_CASE("LinkGraph: backlinks reverse edges", "[link_graph]") {
    LinkGraph lg;
    lg.index_page("/a/", "Alpha");
    lg.index_page("/b/", "Beta");
    lg.index_page("/c/", "Gamma");

    // A -> B, C -> B. B should have two backlinks.
    {
        std::string md = "[[b]]";
        auto links = lg.rewrite_wikilinks(md, "/a/");
        lg.add_outgoing("/a/", links);
    }
    {
        std::string md = "[[b]]";
        auto links = lg.rewrite_wikilinks(md, "/c/");
        lg.add_outgoing("/c/", links);
    }

    auto backlinks = lg.get_backlinks("/b/");
    REQUIRE(backlinks.is_array());
    REQUIRE(backlinks.size() == 2);
    // Sorted by source title: "Alpha" < "Gamma".
    REQUIRE(backlinks[0]["title"] == "Alpha");
    REQUIRE(backlinks[0]["url"] == "/a/");
    REQUIRE(backlinks[1]["title"] == "Gamma");
    REQUIRE(backlinks[1]["url"] == "/c/");
}

TEST_CASE("LinkGraph: self-link excluded from backlinks", "[link_graph]") {
    LinkGraph lg;
    lg.index_page("/a/", "Alpha");

    std::string md = "[[a]]";  // self-link
    auto links = lg.rewrite_wikilinks(md, "/a/");
    lg.add_outgoing("/a/", links);

    auto backlinks = lg.get_backlinks("/a/");
    REQUIRE(backlinks.size() == 0);
}

TEST_CASE("LinkGraph: unresolved outgoing edges excluded from backlinks", "[link_graph]") {
    LinkGraph lg;
    lg.index_page("/a/", "Alpha");
    lg.index_page("/b/", "Beta");

    // A -> unresolved; should not contribute to any backlink list.
    {
        std::string md = "[[missing]]";
        auto links = lg.rewrite_wikilinks(md, "/a/");
        lg.add_outgoing("/a/", links);
    }
    // B has no incoming edges.
    auto backlinks_b = lg.get_backlinks("/b/");
    REQUIRE(backlinks_b.size() == 0);

    // Unresolved target itself has no backlinks.
    auto backlinks_missing = lg.get_backlinks("/missing/");
    REQUIRE(backlinks_missing.size() == 0);
}

TEST_CASE("LinkGraph: get_backlinks returns empty for unknown url", "[link_graph]") {
    LinkGraph lg;
    lg.index_page("/a/", "Alpha");

    auto backlinks = lg.get_backlinks("/does-not-exist/");
    REQUIRE(backlinks.is_array());
    REQUIRE(backlinks.size() == 0);
}

TEST_CASE("LinkGraph: get_backlinks returns empty array for empty url", "[link_graph]") {
    LinkGraph lg;
    auto backlinks = lg.get_backlinks("");
    REQUIRE(backlinks.is_array());
    REQUIRE(backlinks.size() == 0);
}

TEST_CASE("LinkGraph: serialize_index is deterministic", "[link_graph]") {
    auto build = []() {
        LinkGraph lg;
        lg.index_page("/b/", "Beta");
        lg.index_page("/a/", "Alpha", {"alias-one"});
        lg.index_page("/c/", "Gamma");
        return lg.serialize_index();
    };

    std::string first = build();
    std::string second = build();
    REQUIRE(first == second);
    // Sanity: the serialization actually has content.
    REQUIRE(first.find("slug:") != std::string::npos);
    REQUIRE(first.find("title:") != std::string::npos);
    REQUIRE(first.find("alias:") != std::string::npos);
    // Display titles are hashed via the url_title: section so case-only
    // title changes invalidate downstream pages.
    REQUIRE(first.find("url_title:") != std::string::npos);
    REQUIRE(first.find("Alpha") != std::string::npos);
    REQUIRE(first.find("alias-one") != std::string::npos);
}

TEST_CASE("LinkGraph: serialize_index differs when aliases differ", "[link_graph]") {
    LinkGraph a, b;
    a.index_page("/p/", "Page", {"one"});
    b.index_page("/p/", "Page", {"two"});
    REQUIRE(a.serialize_index() != b.serialize_index());
}

TEST_CASE("LinkGraph: index_page ignores empty url", "[link_graph]") {
    LinkGraph lg;
    lg.index_page("", "Empty URL");
    // Empty URL indexes nothing — no crash, no entries.
    std::string md = "[[Empty URL]]";
    auto encountered = lg.rewrite_wikilinks(md, "/src/");
    REQUIRE(encountered[0].resolved_url.empty());
}

TEST_CASE("LinkGraph: empty markdown returns no wikilinks", "[link_graph]") {
    LinkGraph lg;
    lg.index_page("/a/", "Alpha");

    std::string md;
    auto encountered = lg.rewrite_wikilinks(md, "/src/");
    REQUIRE(encountered.empty());
    REQUIRE(md.empty());
}
