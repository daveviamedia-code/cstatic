#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

#include <httplib.h>

#include "pipeline/link_checker.hpp"

namespace fs = std::filesystem;
using cstatic::pipeline::check_links;
using cstatic::pipeline::CheckResult;
using cstatic::pipeline::LinkIssue;

namespace {

// Per-test temp output/ directory. Tests chdir into the root so relative
// paths resolve correctly, then restore the original cwd on teardown.
struct LinkFixture {
    std::string root_dir;
    std::string saved_cwd;

    LinkFixture() {
        char buf[4096];
        saved_cwd = getcwd(buf, sizeof(buf));
        root_dir = (fs::temp_directory_path() /
                    ("cstatic_lc_" + std::to_string(std::rand()))).string();
        fs::create_directories(root_dir);
        chdir(root_dir.c_str());
    }

    ~LinkFixture() {
        chdir(saved_cwd.c_str());
        std::error_code ec;
        fs::remove_all(root_dir, ec);
    }

    std::string output_dir() const { return root_dir + "/output"; }

    // Write a file under output/, creating parent dirs. Returns absolute path.
    std::string write(const std::string& rel, const std::string& content) {
        fs::path p = fs::path(output_dir()) / rel;
        fs::create_directories(p.parent_path());
        std::ofstream f(p);
        f << content;
        f.close();
        REQUIRE(f.good());
        return fs::weakly_canonical(p).string();
    }

    static std::string read(const std::string& path) {
        std::ifstream f(path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
};

// Embedded HTTP server bound to 127.0.0.1 on an auto-selected port.
// Register routes via route() BEFORE calling start().
struct LocalServer {
    httplib::Server svr;
    std::thread thread;
    int port = 0;

    LocalServer() {
        unsigned seed = static_cast<unsigned>(std::time(nullptr)) ^ static_cast<unsigned>(std::rand());
        std::srand(seed);
        int base = 30000 + (std::rand() % 20000);
        for (int attempt = 0; attempt < 200; ++attempt) {
            int candidate = base + attempt;
            if (candidate > 65000) candidate = 20000 + attempt;
            if (svr.bind_to_port("127.0.0.1", candidate)) {
                port = candidate;
                return;
            }
        }
    }

    // Register a handler — httplib dispatches both GET and HEAD through the
    // same get_handlers_ table (see Server::routing), so a single Get()
    // registration is enough for HEAD-first probing.
    template <typename Handler>
    void route(const std::string& pattern, Handler h) {
        svr.Get(pattern, h);
    }

    void start() {
        // Internal health endpoint used only for readiness polling.
        svr.Get("/__lc_health", [](const httplib::Request&, httplib::Response& res) {
            res.status = 200;
            res.set_content("ok", "text/plain");
        });
        thread = std::thread([this] { svr.listen_after_bind(); });
        // Poll until the server is accepting connections.
        for (int i = 0; i < 300; ++i) {
            httplib::Client cli("127.0.0.1", port);
            cli.set_connection_timeout(0, 200000);  // 200ms
            auto res = cli.Get("/__lc_health");
            if (res && res->status == 200) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        FAIL("LocalServer did not become ready");
    }

    ~LocalServer() {
        svr.stop();
        if (thread.joinable()) thread.join();
    }

    std::string base() const { return "http://127.0.0.1:" + std::to_string(port); }
};

// Find the first issue whose href matches a substring; returns nullptr if none.
const LinkIssue* find_issue(const CheckResult& r, const std::string& href_substr) {
    for (const auto& issue : r.issues) {
        if (issue.href.find(href_substr) != std::string::npos) return &issue;
    }
    return nullptr;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Internal link verification
// ---------------------------------------------------------------------------

TEST_CASE("LinkChecker: internal broken link reported with file:line", "[link_checker]") {
    LinkFixture fx;
    fx.write("index.html",
        "<html>\n"
        "<body>\n"
        "  <a href=\"/missing.html\">broken</a>\n"  // line 3
        "</body>\n"
        "</html>\n");

    CheckResult r = check_links(fx.output_dir(), false, 5000);
    REQUIRE(r.issues.size() == 1);
    REQUIRE(find_issue(r, "/missing.html") != nullptr);
    REQUIRE(r.issues[0].line == 3);
    REQUIRE(r.issues[0].is_external == false);
    REQUIRE(r.internal_checked == 1);
}

TEST_CASE("LinkChecker: valid internal link passes", "[link_checker]") {
    LinkFixture fx;
    fx.write("posts/foo.html", "<h1>Foo</h1>");
    fx.write("index.html", R"(<a href="/posts/foo.html">ok</a>)");

    CheckResult r = check_links(fx.output_dir(), false, 5000);
    REQUIRE(r.issues.empty());
    REQUIRE(r.internal_checked == 1);
}

TEST_CASE("LinkChecker: fragment stripped before resolving", "[link_checker]") {
    LinkFixture fx;
    fx.write("posts/foo.html", "<h1>Foo</h1>");
    fx.write("index.html", R"(<a href="/posts/foo.html#section">ok</a>)");

    CheckResult r = check_links(fx.output_dir(), false, 5000);
    REQUIRE(r.issues.empty());
    REQUIRE(r.internal_checked == 1);
}

TEST_CASE("LinkChecker: query string stripped before resolving", "[link_checker]") {
    LinkFixture fx;
    fx.write("posts/foo.html", "<h1>Foo</h1>");
    fx.write("index.html", R"(<a href="/posts/foo.html?utm=x">ok</a>)");

    CheckResult r = check_links(fx.output_dir(), false, 5000);
    REQUIRE(r.issues.empty());
    REQUIRE(r.internal_checked == 1);
}

TEST_CASE("LinkChecker: directory URL resolves to index.html", "[link_checker]") {
    LinkFixture fx;
    fx.write("posts/index.html", "<h1>Posts</h1>");
    fx.write("index.html", R"(<a href="/posts/">posts</a>)");

    CheckResult r = check_links(fx.output_dir(), false, 5000);
    REQUIRE(r.issues.empty());
    REQUIRE(r.internal_checked == 1);
}

TEST_CASE("LinkChecker: src attributes are scanned", "[link_checker]") {
    LinkFixture fx;
    fx.write("img/a.png", "PNG");  // binary placeholder, existence is all that matters
    fx.write("index.html", R"(<img src="/img/a.png" alt="a">)");

    CheckResult r = check_links(fx.output_dir(), false, 5000);
    REQUIRE(r.issues.empty());
    REQUIRE(r.internal_checked == 1);
}

// ---------------------------------------------------------------------------
// Skipped URL categories
// ---------------------------------------------------------------------------

TEST_CASE("LinkChecker: mailto/tel/data/javascript skipped", "[link_checker]") {
    LinkFixture fx;
    fx.write("index.html",
        "<a href=\"mailto:a@b.com\">m</a>\n"
        "<a href=\"tel:+15551234\">t</a>\n"
        "<a href=\"data:text/plain,hello\">d</a>\n"
        "<a href=\"javascript:void(0)\">j</a>\n");

    CheckResult r = check_links(fx.output_dir(), false, 5000);
    REQUIRE(r.issues.empty());
    REQUIRE(r.internal_checked == 0);
    REQUIRE(r.external_checked == 0);
    // Total counts every href/src encountered, even skippable ones.
    REQUIRE(r.total_links == 4);
}

TEST_CASE("LinkChecker: in-page fragment-only skipped", "[link_checker]") {
    LinkFixture fx;
    fx.write("index.html", R"(<a href="#section">x</a>)");

    CheckResult r = check_links(fx.output_dir(), false, 5000);
    REQUIRE(r.issues.empty());
    REQUIRE(r.internal_checked == 0);
}

TEST_CASE("LinkChecker: relative URLs skipped in v1", "[link_checker]") {
    LinkFixture fx;
    fx.write("index.html", R"(<a href="other.html">rel</a>)");

    CheckResult r = check_links(fx.output_dir(), false, 5000);
    REQUIRE(r.issues.empty());
    REQUIRE(r.internal_checked == 0);
    REQUIRE(r.external_checked == 0);
}

// ---------------------------------------------------------------------------
// External link checking
// ---------------------------------------------------------------------------

TEST_CASE("LinkChecker: external skipped when check_external=false", "[link_checker]") {
    LinkFixture fx;
    fx.write("index.html", R"(<a href="http://127.0.0.1:1/nope">x</a>)");

    CheckResult r = check_links(fx.output_dir(), false, 5000);
    REQUIRE(r.issues.empty());
    REQUIRE(r.external_checked == 0);
    REQUIRE(r.internal_checked == 0);
}

TEST_CASE("LinkChecker: external 200 produces no issue", "[link_checker]") {
    LinkFixture fx;
    LocalServer srv;
    srv.route("/ok", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content("ok", "text/plain");
    });
    srv.start();

    fx.write("index.html", "<a href=\"" + srv.base() + "/ok\">x</a>");

    CheckResult r = check_links(fx.output_dir(), true, 5000);
    REQUIRE(r.issues.empty());
    REQUIRE(r.external_checked == 1);
}

TEST_CASE("LinkChecker: external 404 reported as issue", "[link_checker]") {
    LinkFixture fx;
    LocalServer srv;
    srv.route("/missing", [](const httplib::Request&, httplib::Response& res) {
        res.status = 404;
        res.set_content("nope", "text/plain");
    });
    srv.start();

    fx.write("index.html", "<a href=\"" + srv.base() + "/missing\">x</a>");

    CheckResult r = check_links(fx.output_dir(), true, 5000);
    REQUIRE(r.issues.size() == 1);
    REQUIRE(find_issue(r, "/missing") != nullptr);
    REQUIRE(r.issues[0].is_external == true);
    REQUIRE(r.external_checked == 1);
}

TEST_CASE("LinkChecker: external redirect to 200 resolves", "[link_checker]") {
    LinkFixture fx;
    LocalServer srv;
    srv.route("/target", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content("ok", "text/plain");
    });
    srv.route("/src", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/target");
    });
    srv.start();

    fx.write("index.html", "<a href=\"" + srv.base() + "/src\">x</a>");

    CheckResult r = check_links(fx.output_dir(), true, 5000);
    REQUIRE(r.issues.empty());
    REQUIRE(r.external_checked == 1);
}

TEST_CASE("LinkChecker: external URL deduplicated across files", "[link_checker]") {
    LinkFixture fx;
    LocalServer srv;
    std::atomic<int> hits{0};
    srv.route("/shared", [&hits](const httplib::Request&, httplib::Response& res) {
        ++hits;
        res.status = 200;
        res.set_content("ok", "text/plain");
    });
    srv.start();

    std::string url = srv.base() + "/shared";
    fx.write("a.html", "<a href=\"" + url + "\">a</a>");
    fx.write("b.html", "<a href=\"" + url + "\">b</a>");

    CheckResult r = check_links(fx.output_dir(), true, 5000);
    REQUIRE(r.issues.empty());
    REQUIRE(r.external_checked == 1);    // one HTTP request despite 2 occurrences
    REQUIRE(r.total_links == 2);
    // Allow the server a moment to record the hit before checking.
    for (int i = 0; i < 50 && hits.load() < 1; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(hits.load() == 1);
}

TEST_CASE("LinkChecker: line number correct for multi-line HTML", "[link_checker]") {
    LinkFixture fx;
    // Line 1: <!doctype>
    // Line 2: <html>
    // Line 3: <body>
    // Line 4: <a href="/missing-line-4.html">
    fx.write("index.html",
        "<!doctype html>\n"
        "<html>\n"
        "<body>\n"
        "  <a href=\"/missing-line-4.html\">x</a>\n"
        "</body>\n"
        "</html>\n");

    CheckResult r = check_links(fx.output_dir(), false, 5000);
    REQUIRE(r.issues.size() == 1);
    REQUIRE(r.issues[0].line == 4);
    REQUIRE(r.issues[0].href == "/missing-line-4.html");
}
