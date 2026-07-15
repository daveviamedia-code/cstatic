#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "config/config.hpp"
#include "modules/well_known.hpp"
#include "utils/file_io.hpp"
#include "test_util.hpp"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using cstatic::Config;
using cstatic::modules::generate_well_known;
using cstatic::utils::read_file;

namespace {

// RAII temp dir for .well-known/ output.
struct WkDir {
    fs::path dir;
    WkDir() : dir(cstatic_test::unique_temp_dir("cstatic_wk_")) {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~WkDir() { fs::remove_all(dir); }
    std::string path() const { return dir.string(); }
    std::string read(const std::string& name) const {
        return read_file((dir / name).string());
    }
    bool exists(const std::string& name) const {
        return fs::exists(dir / name);
    }
};

static Config base_config() {
    Config cfg;
    cfg.site_title = "My Site";
    cfg.site_base_url = "https://example.com";
    return cfg;
}

} // anonymous namespace

TEST_CASE("well_known: nothing written when both disabled", "[well_known]") {
    WkDir d;
    Config cfg = base_config();  // both flags default false
    generate_well_known(cfg, d.path());
    REQUIRE(!fs::exists(d.dir / ".well-known"));
}

TEST_CASE("well_known: ai-plugin.json is valid JSON", "[well_known]") {
    WkDir d;
    Config cfg = base_config();
    cfg.wk_ai_plugin_enabled = true;
    generate_well_known(cfg, d.path());
    REQUIRE(d.exists(".well-known/ai-plugin.json"));
    // Parses without throwing.
    auto j = nlohmann::json::parse(d.read(".well-known/ai-plugin.json"));
    REQUIRE(j["schema_version"] == "v1");
    REQUIRE(j["name_for_human"] == "My Site");
}

TEST_CASE("well_known: name defaults to site_title", "[well_known]") {
    WkDir d;
    Config cfg = base_config();
    cfg.wk_ai_plugin_enabled = true;
    generate_well_known(cfg, d.path());
    auto j = nlohmann::json::parse(d.read(".well-known/ai-plugin.json"));
    REQUIRE(j["name_for_human"] == "My Site");
    REQUIRE(j["name_for_model"] == "my-site");
}

TEST_CASE("well_known: explicit name overrides site_title", "[well_known]") {
    WkDir d;
    Config cfg = base_config();
    cfg.wk_ai_plugin_enabled = true;
    cfg.wk_ai_plugin_name = "Acme Blog Pro";
    generate_well_known(cfg, d.path());
    auto j = nlohmann::json::parse(d.read(".well-known/ai-plugin.json"));
    REQUIRE(j["name_for_human"] == "Acme Blog Pro");
    REQUIRE(j["name_for_model"] == "acme-blog-pro");
}

TEST_CASE("well_known: description defaults to site_description", "[well_known]") {
    WkDir d;
    Config cfg = base_config();
    cfg.wk_ai_plugin_enabled = true;
    cfg.site_description = "A blog about things.";
    generate_well_known(cfg, d.path());
    auto j = nlohmann::json::parse(d.read(".well-known/ai-plugin.json"));
    REQUIRE(j["description_for_human"] == "A blog about things.");
    REQUIRE(j["description_for_model"] == "A blog about things.");
}

TEST_CASE("well_known: empty description omits description fields", "[well_known]") {
    WkDir d;
    Config cfg = base_config();
    cfg.wk_ai_plugin_enabled = true;  // no description of any kind
    generate_well_known(cfg, d.path());
    auto j = nlohmann::json::parse(d.read(".well-known/ai-plugin.json"));
    REQUIRE(!j.contains("description_for_human"));
    REQUIRE(!j.contains("description_for_model"));
}

TEST_CASE("well_known: api.url points at site openapi.json", "[well_known]") {
    WkDir d;
    Config cfg = base_config();
    cfg.wk_ai_plugin_enabled = true;
    generate_well_known(cfg, d.path());
    auto j = nlohmann::json::parse(d.read(".well-known/ai-plugin.json"));
    REQUIRE(j["api"]["type"] == "openapi");
    REQUIRE(j["api"]["url"] == "https://example.com/openapi.json");
}

TEST_CASE("well_known: auth is none for static sites", "[well_known]") {
    WkDir d;
    Config cfg = base_config();
    cfg.wk_ai_plugin_enabled = true;
    generate_well_known(cfg, d.path());
    auto j = nlohmann::json::parse(d.read(".well-known/ai-plugin.json"));
    REQUIRE(j["auth"]["type"] == "none");
}

TEST_CASE("well_known: logo_url resolved from org_logo", "[well_known]") {
    WkDir d;
    Config cfg = base_config();
    cfg.wk_ai_plugin_enabled = true;
    cfg.org_logo = "/images/logo.png";
    generate_well_known(cfg, d.path());
    auto j = nlohmann::json::parse(d.read(".well-known/ai-plugin.json"));
    REQUIRE(j["logo_url"] == "https://example.com/images/logo.png");
}

TEST_CASE("well_known: absolute org_logo kept as-is", "[well_known]") {
    WkDir d;
    Config cfg = base_config();
    cfg.wk_ai_plugin_enabled = true;
    cfg.org_logo = "https://cdn.example.com/logo.png";
    generate_well_known(cfg, d.path());
    auto j = nlohmann::json::parse(d.read(".well-known/ai-plugin.json"));
    REQUIRE(j["logo_url"] == "https://cdn.example.com/logo.png");
}

TEST_CASE("well_known: logo_url omitted when no org_logo", "[well_known]") {
    WkDir d;
    Config cfg = base_config();
    cfg.wk_ai_plugin_enabled = true;
    generate_well_known(cfg, d.path());
    auto j = nlohmann::json::parse(d.read(".well-known/ai-plugin.json"));
    REQUIRE(!j.contains("logo_url"));
}

TEST_CASE("well_known: security.txt written verbatim", "[well_known]") {
    WkDir d;
    Config cfg = base_config();
    cfg.wk_security_txt_enabled = true;
    cfg.wk_security_txt_content = "Contact: mailto:security@example.com\nExpires: 2026-12-31T23:59:59.000Z\n";
    generate_well_known(cfg, d.path());
    REQUIRE(d.exists(".well-known/security.txt"));
    REQUIRE(d.read(".well-known/security.txt") == cfg.wk_security_txt_content);
}

TEST_CASE("well_known: ai-plugin only does not create security.txt", "[well_known]") {
    WkDir d;
    Config cfg = base_config();
    cfg.wk_ai_plugin_enabled = true;
    generate_well_known(cfg, d.path());
    REQUIRE(d.exists(".well-known/ai-plugin.json"));
    REQUIRE(!d.exists(".well-known/security.txt"));
}

TEST_CASE("well_known: security.txt only does not create ai-plugin.json", "[well_known]") {
    WkDir d;
    Config cfg = base_config();
    cfg.wk_security_txt_enabled = true;
    cfg.wk_security_txt_content = "Contact: mailto:x@example.com\n";
    generate_well_known(cfg, d.path());
    REQUIRE(d.exists(".well-known/security.txt"));
    REQUIRE(!d.exists(".well-known/ai-plugin.json"));
}

TEST_CASE("well_known: schema_version override honored", "[well_known]") {
    WkDir d;
    Config cfg = base_config();
    cfg.wk_ai_plugin_enabled = true;
    cfg.wk_ai_plugin_schema_version = "v2";
    generate_well_known(cfg, d.path());
    auto j = nlohmann::json::parse(d.read(".well-known/ai-plugin.json"));
    REQUIRE(j["schema_version"] == "v2");
}
