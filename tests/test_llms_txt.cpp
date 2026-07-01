#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "config/config.hpp"
#include "modules/llms_txt.hpp"
#include "utils/file_io.hpp"
#include "test_util.hpp"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using cstatic::Config;
using cstatic::modules::generate_llms_txt;
using cstatic::utils::read_file;

namespace {

// RAII temp dir for llms.txt output.
struct LlmsDir {
    fs::path dir;
    LlmsDir() : dir(cstatic_test::unique_temp_dir("cstatic_llms_")) {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~LlmsDir() { fs::remove_all(dir); }
    std::string path() const { return dir.string(); }
    std::string read(const std::string& name) const {
        return read_file((dir / name).string());
    }
};

static bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

static int count_of(const std::string& h, const std::string& needle) {
    int n = 0;
    size_t pos = 0;
    while ((pos = h.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
}

static Config base_config() {
    Config cfg;
    cfg.site_title = "My Site";
    cfg.site_base_url = "https://example.com";
    cfg.module_llms_txt = true;
    return cfg;
}

static nlohmann::json make_page(const std::string& title, const std::string& url,
                                const std::string& date, const std::string& excerpt) {
    nlohmann::json p;
    p["title"] = title;
    p["url"] = url;
    p["date"] = date;
    p["excerpt"] = excerpt;
    return p;
}

} // anonymous namespace

TEST_CASE("llms.txt: both files written", "[llms_txt]") {
    LlmsDir d;
    Config cfg = base_config();
    nlohmann::json pages = nlohmann::json::array({
        make_page("P", "/p/", "2025-01-01", "e."),
    });
    generate_llms_txt(cfg, pages, d.path());
    REQUIRE(fs::exists(d.dir / "llms.txt"));
    REQUIRE(fs::exists(d.dir / "llms-full.txt"));
}

TEST_CASE("llms.txt: header format with description", "[llms_txt]") {
    LlmsDir d;
    Config cfg = base_config();
    cfg.llms_txt_description = "A site about things.";
    nlohmann::json pages = nlohmann::json::array({
        make_page("First", "/first/", "2025-01-01", "First excerpt."),
    });
    generate_llms_txt(cfg, pages, d.path());
    const std::string out = d.read("llms.txt");

    REQUIRE(contains(out, "# My Site\n\n"));
    REQUIRE(contains(out, "> A site about things.\n\n"));
    REQUIRE(contains(out, "## Pages:\n"));
}

TEST_CASE("llms.txt: description falls back to site_description", "[llms_txt]") {
    LlmsDir d;
    Config cfg = base_config();
    cfg.site_description = "Site-level description.";  // llms_txt_description left empty
    nlohmann::json pages = nlohmann::json::array({
        make_page("P", "/p/", "2025-01-01", "ex."),
    });
    generate_llms_txt(cfg, pages, d.path());
    REQUIRE(contains(d.read("llms.txt"), "> Site-level description.\n"));
}

TEST_CASE("llms.txt: missing description omits the > line", "[llms_txt]") {
    LlmsDir d;
    Config cfg = base_config();  // no description of any kind
    nlohmann::json pages = nlohmann::json::array({
        make_page("P", "/p/", "2025-01-01", "ex."),
    });
    generate_llms_txt(cfg, pages, d.path());
    const std::string out = d.read("llms.txt");
    REQUIRE(!contains(out, "> "));
    // Title is followed by a blank line then the Pages heading.
    REQUIRE(contains(out, "# My Site\n\n## Pages:\n"));
}

TEST_CASE("llms.txt: page ordering preserved", "[llms_txt]") {
    LlmsDir d;
    Config cfg = base_config();
    // Caller passes pages already sorted newest-first (builder does this).
    nlohmann::json pages = nlohmann::json::array({
        make_page("Newer", "/newer/", "2025-05-01", "n."),
        make_page("Older", "/older/", "2024-01-01", "o."),
    });
    generate_llms_txt(cfg, pages, d.path());
    const std::string out = d.read("llms.txt");
    REQUIRE(out.find("/newer/") < out.find("/older/"));
}

TEST_CASE("llms.txt: exclusion glob drops matching pages", "[llms_txt]") {
    LlmsDir d;
    Config cfg = base_config();
    cfg.llms_txt_exclude = {"/tags/*"};
    nlohmann::json pages = nlohmann::json::array({
        make_page("Kept", "/posts/kept/", "2025-01-01", "k."),
        make_page("Tag", "/tags/foo/", "2025-01-02", "t."),
    });
    generate_llms_txt(cfg, pages, d.path());
    const std::string out = d.read("llms.txt");
    REQUIRE(contains(out, "/posts/kept/"));
    REQUIRE(!contains(out, "/tags/foo/"));
}

TEST_CASE("llms.txt: exclusion applies to both files", "[llms_txt]") {
    LlmsDir d;
    Config cfg = base_config();
    cfg.llms_txt_exclude = {"/private/*"};
    nlohmann::json pages = nlohmann::json::array({
        make_page("Kept", "/kept/", "2025-01-01", "k."),
        make_page("Priv", "/private/secret/", "2025-01-02", "p."),
    });
    generate_llms_txt(cfg, pages, d.path());
    REQUIRE(!contains(d.read("llms.txt"), "/private/"));
    REQUIRE(!contains(d.read("llms-full.txt"), "/private/"));
    REQUIRE(contains(d.read("llms-full.txt"), "/kept/"));
}

TEST_CASE("llms.txt: max_pages caps compact but not full", "[llms_txt]") {
    LlmsDir d;
    Config cfg = base_config();
    cfg.llms_txt_max_pages = 1;
    nlohmann::json pages = nlohmann::json::array({
        make_page("A", "/a/", "2025-01-01", "a."),
        make_page("B", "/b/", "2025-01-02", "b."),
        make_page("C", "/c/", "2025-01-03", "c."),
    });
    generate_llms_txt(cfg, pages, d.path());

    REQUIRE(count_of(d.read("llms.txt"), "- [") == 1);
    REQUIRE(count_of(d.read("llms-full.txt"), "- [") == 3);
}

TEST_CASE("llms.txt: max_pages zero means no cap", "[llms_txt]") {
    LlmsDir d;
    Config cfg = base_config();  // llms_txt_max_pages defaults to 0
    nlohmann::json pages = nlohmann::json::array({
        make_page("A", "/a/", "2025-01-01", "a."),
        make_page("B", "/b/", "2025-01-02", "b."),
    });
    generate_llms_txt(cfg, pages, d.path());
    REQUIRE(count_of(d.read("llms.txt"), "- [") == 2);
}

TEST_CASE("llms.txt: entry format with excerpt", "[llms_txt]") {
    LlmsDir d;
    Config cfg = base_config();
    nlohmann::json pages = nlohmann::json::array({
        make_page("Hello", "/hello/", "2025-01-01", "A short summary."),
    });
    generate_llms_txt(cfg, pages, d.path());
    REQUIRE(contains(d.read("llms.txt"),
                     "- [Hello](https://example.com/hello/): A short summary.\n"));
}

TEST_CASE("llms.txt: entry without excerpt has no trailing colon", "[llms_txt]") {
    LlmsDir d;
    Config cfg = base_config();
    nlohmann::json pages = nlohmann::json::array({
        make_page("NoEx", "/noex/", "2025-01-01", ""),
    });
    generate_llms_txt(cfg, pages, d.path());
    REQUIRE(contains(d.read("llms.txt"), "- [NoEx](https://example.com/noex/)\n"));
    REQUIRE(!contains(d.read("llms.txt"), "/noex/):"));
}

TEST_CASE("llms.txt: pages with empty url or title are skipped", "[llms_txt]") {
    LlmsDir d;
    Config cfg = base_config();
    nlohmann::json pages = nlohmann::json::array({
        make_page("", "/notitle/", "2025-01-01", "x."),
        make_page("NoUrl", "", "2025-01-01", "y."),
        make_page("Valid", "/valid/", "2025-01-01", "z."),
    });
    generate_llms_txt(cfg, pages, d.path());
    const std::string out = d.read("llms.txt");
    REQUIRE(contains(out, "/valid/"));
    REQUIRE(!contains(out, "/notitle/"));
    REQUIRE(!contains(out, "NoUrl"));
}

TEST_CASE("llms.txt: excerpt truncated well below original length", "[llms_txt]") {
    LlmsDir d;
    Config cfg = base_config();
    std::string long_excerpt;
    for (int i = 0; i < 40; ++i) long_excerpt += "word ";  // ~200 chars, no ellipsis
    nlohmann::json pages = nlohmann::json::array({
        make_page("Long", "/long/", "2025-01-01", long_excerpt),
    });
    generate_llms_txt(cfg, pages, d.path());
    const std::string out = d.read("llms.txt");

    auto line_pos = out.find("- [Long]");
    REQUIRE(line_pos != std::string::npos);
    auto nl = out.find('\n', line_pos);
    std::string line = out.substr(line_pos, nl - line_pos);
    auto colon = line.find(": ");
    REQUIRE(colon != std::string::npos);
    std::string summary = line.substr(colon + 2);
    // utils::truncate_text(s, 160) returns at most substr(0, <=160) + "...".
    REQUIRE(summary.size() <= 163);
    REQUIRE(summary.size() < long_excerpt.size());
}

TEST_CASE("llms.txt: newlines in excerpt collapsed onto one line", "[llms_txt]") {
    LlmsDir d;
    Config cfg = base_config();
    nlohmann::json pages = nlohmann::json::array({
        make_page("Multi", "/multi/", "2025-01-01", "line one\nline two"),
    });
    generate_llms_txt(cfg, pages, d.path());
    const std::string out = d.read("llms.txt");
    REQUIRE(contains(out, "line one line two"));
    // No bare newline between "line one" and "line two" on the entry line.
    auto p = out.find("line one");
    REQUIRE(p != std::string::npos);
    REQUIRE(out.substr(p, 13) == "line one line");
}
