#include <catch2/catch_test_macros.hpp>
#include "assets/asset_pipeline.hpp"
#include "template/renderer.hpp"

#include <stb_image.h>
#include <stb_image_write.h>
#include <fstream>
#include <filesystem>

using namespace cstatic;

TEST_CASE("Minifier: CSS comment removal", "[minifier]") {
    std::string css = "/* comment */ body { color: red; }";
    std::string result = minify_css(css);
    REQUIRE(result.find("/*") == std::string::npos);
    REQUIRE(result.find("comment") == std::string::npos);
    REQUIRE(result.find("color:red") != std::string::npos);
}

TEST_CASE("Minifier: CSS whitespace collapse", "[minifier]") {
    std::string css = "body  {\n  color  :  red  ;\n  margin  :  0  ;\n}";
    std::string result = minify_css(css);
    // Should have no newlines
    REQUIRE(result.find('\n') == std::string::npos);
    // Should be significantly shorter
    REQUIRE(result.size() < css.size());
}

TEST_CASE("Minifier: CSS semicolon before closing brace removed", "[minifier]") {
    std::string css = "body { color: red; }";
    std::string result = minify_css(css);
    REQUIRE(result.find(";}") == std::string::npos);
    REQUIRE(result.find("color:red") != std::string::npos);
}

TEST_CASE("Minifier: CSS preserves selector-content space", "[minifier]") {
    std::string css = "body { color: red; } div { margin: 0; }";
    std::string result = minify_css(css);
    REQUIRE(result.find("body{color:red}div{margin:0}") != std::string::npos);
}

TEST_CASE("Minifier: CSS empty rules removed", "[minifier]") {
    std::string css = "body {} div { color: red; }";
    std::string result = minify_css(css);
    REQUIRE(result.find("body") == std::string::npos);
    REQUIRE(result.find("div{color:red}") != std::string::npos);
}

TEST_CASE("Minifier: CSS empty input", "[minifier]") {
    REQUIRE(minify_css("") == "");
}

TEST_CASE("Minifier: JS single-line comment removal", "[minifier]") {
    std::string js = "var x = 1; // this is a comment\nvar y = 2;";
    std::string result = minify_js(js);
    REQUIRE(result.find("//") == std::string::npos);
    REQUIRE(result.find("this is a comment") == std::string::npos);
    REQUIRE(result.find("var x=1") != std::string::npos);
}

TEST_CASE("Minifier: JS block comment removal", "[minifier]") {
    std::string js = "var x = 1; /* block\ncomment */ var y = 2;";
    std::string result = minify_js(js);
    REQUIRE(result.find("/*") == std::string::npos);
    REQUIRE(result.find("block") == std::string::npos);
}

TEST_CASE("Minifier: JS preserves string literals", "[minifier]") {
    std::string js = "var s = \"hello // world\";";
    std::string result = minify_js(js);
    REQUIRE(result.find("\"hello // world\"") != std::string::npos);
}

TEST_CASE("Minifier: JS preserves single-quoted strings", "[minifier]") {
    std::string js = "var s = 'hello // world';";
    std::string result = minify_js(js);
    REQUIRE(result.find("'hello // world'") != std::string::npos);
}

TEST_CASE("Minifier: JS preserves template literals", "[minifier]") {
    std::string js = "var s = `hello // ${name}`;";
    std::string result = minify_js(js);
    REQUIRE(result.find("//") != std::string::npos); // preserved inside backtick string
}

TEST_CASE("Minifier: JS whitespace collapse", "[minifier]") {
    std::string js = "var  x  =  1;\nvar  y  =  2;";
    std::string result = minify_js(js);
    REQUIRE(result.find('\n') == std::string::npos);
    REQUIRE(result.find("var x=1") != std::string::npos);
}

TEST_CASE("Minifier: JS empty input", "[minifier]") {
    REQUIRE(minify_js("") == "");
}

TEST_CASE("Minifier: JS string with escape sequences", "[minifier]") {
    std::string js = "var s = \"hello \\\"world\\\"\";";
    std::string result = minify_js(js);
    REQUIRE(result.find("\\\"world\\\"") != std::string::npos);
}

// --- JS Regex Literal Tests ---

TEST_CASE("Minifier: JS basic regex preserved", "[minifier]") {
    std::string js = "var r = /pattern/g;";
    std::string result = minify_js(js);
    REQUIRE(result.find("/pattern/g") != std::string::npos);
}

TEST_CASE("Minifier: JS regex with escape", "[minifier]") {
    std::string js = "var r = /\\/path\\//g;";
    std::string result = minify_js(js);
    REQUIRE(result.find("/\\/path\\//g") != std::string::npos);
}

TEST_CASE("Minifier: JS regex in condition", "[minifier]") {
    std::string js = "if (/test/.test(str)) { }";
    std::string result = minify_js(js);
    REQUIRE(result.find("/test/") != std::string::npos);
}

TEST_CASE("Minifier: JS division not confused with regex", "[minifier]") {
    std::string js = "var x = 10 / 2;";
    std::string result = minify_js(js);
    REQUIRE(result.find("10/2") != std::string::npos);
    REQUIRE(result.find("//") == std::string::npos); // should not look like a comment
}

TEST_CASE("Minifier: JS regex after return", "[minifier]") {
    std::string js = "return /pattern/;";
    std::string result = minify_js(js);
    REQUIRE(result.find("/pattern/") != std::string::npos);
}

TEST_CASE("Minifier: JS regex after typeof", "[minifier]") {
    std::string js = "typeof /pattern/";
    std::string result = minify_js(js);
    REQUIRE(result.find("/pattern/") != std::string::npos);
}

TEST_CASE("Minifier: JS regex in array", "[minifier]") {
    std::string js = "var a = [/abc/, /def/];";
    std::string result = minify_js(js);
    REQUIRE(result.find("/abc/") != std::string::npos);
    REQUIRE(result.find("/def/") != std::string::npos);
}

TEST_CASE("Minifier: JS chained division stays as division", "[minifier]") {
    std::string js = "var x = a / b / c;";
    std::string result = minify_js(js);
    REQUIRE(result.find("a/b/c") != std::string::npos);
}

// --- HTML Minification Tests ---

TEST_CASE("Minifier: HTML comment removal", "[minifier]") {
    std::string html = "<div>Hello</div><!-- comment --><div>World</div>";
    std::string result = minify_html(html);
    REQUIRE(result.find("<!--") == std::string::npos);
    REQUIRE(result.find("comment") == std::string::npos);
    REQUIRE(result.find("<div>Hello</div>") != std::string::npos);
    REQUIRE(result.find("<div>World</div>") != std::string::npos);
}

TEST_CASE("Minifier: HTML conditional comment preserved", "[minifier]") {
    std::string html = "<!--[if IE]><p>IE</p><![endif]-->";
    std::string result = minify_html(html);
    REQUIRE(result.find("<!--[if IE]><p>IE</p><![endif]-->") != std::string::npos);
}

TEST_CASE("Minifier: HTML whitespace collapse", "[minifier]") {
    std::string html = "<div>  \n  Hello  \n  </div>";
    std::string result = minify_html(html);
    REQUIRE(result.find('\n') == std::string::npos);
    REQUIRE(result.size() < html.size());
}

TEST_CASE("Minifier: HTML optional closing tag removal", "[minifier]") {
    std::string html = "<ul><li>one</li><li>two</li></ul>";
    std::string result = minify_html(html);
    REQUIRE(result.find("</li>") == std::string::npos);
    REQUIRE(result.find("<li>one") != std::string::npos);
    REQUIRE(result.find("<li>two") != std::string::npos);
}

TEST_CASE("Minifier: HTML optional closing p tag removed", "[minifier]") {
    std::string html = "<p>First</p><p>Second</p>";
    std::string result = minify_html(html);
    REQUIRE(result.find("</p>") == std::string::npos);
    REQUIRE(result.find("<p>First") != std::string::npos);
}

TEST_CASE("Minifier: HTML attribute quote removal simple value", "[minifier]") {
    std::string html = "<div class=\"container\">";
    std::string result = minify_html(html);
    REQUIRE(result.find("class=container") != std::string::npos);
    REQUIRE(result.find("\"container\"") == std::string::npos);
}

TEST_CASE("Minifier: HTML attribute quotes preserved for multi-word", "[minifier]") {
    std::string html = "<div class=\"foo bar\">";
    std::string result = minify_html(html);
    REQUIRE(result.find("class=\"foo bar\"") != std::string::npos);
}

TEST_CASE("Minifier: HTML empty input", "[minifier]") {
    REQUIRE(minify_html("") == "");
}

TEST_CASE("Minifier: HTML non-optional closing tags preserved", "[minifier]") {
    std::string html = "<div>Hello</div>";
    std::string result = minify_html(html);
    REQUIRE(result.find("</div>") != std::string::npos);
}

// --- Image Optimization Tests ---

TEST_CASE("Image: content_hash_hex8 produces 8-char hex string", "[image]") {
    std::string hash = content_hash_hex8("hello world");
    REQUIRE(hash.size() == 8);
    // Verify all chars are hex
    for (char c : hash) {
        bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        REQUIRE(is_hex);
    }
}

TEST_CASE("Image: content_hash_hex8 is deterministic", "[image]") {
    std::string hash1 = content_hash_hex8("test content");
    std::string hash2 = content_hash_hex8("test content");
    REQUIRE(hash1 == hash2);
}

TEST_CASE("Image: content_hash_hex8 differs for different content", "[image]") {
    std::string hash1 = content_hash_hex8("content A");
    std::string hash2 = content_hash_hex8("content B");
    REQUIRE(hash1 != hash2);
}

TEST_CASE("Image: optimize_image returns empty for unsupported format", "[image]") {
    int saved = 0;
    std::string result = optimize_image("not an image", 1920, 85, ".txt", saved);
    REQUIRE(result.empty());
}

TEST_CASE("Image: optimize_image produces valid output from PNG input", "[image]") {
    // Create a small 4x4 red image and write to temp PNG
    int w = 4, h = 4, channels = 3;
    std::vector<unsigned char> pixels(w * h * channels, 0);
    for (int i = 0; i < w * h; i++) {
        pixels[i * 3] = 255;     // R
        pixels[i * 3 + 1] = 0;   // G
        pixels[i * 3 + 2] = 0;   // B
    }

    std::string tmp_png = "/tmp/cstatic_test_input.png";
    int ok = stbi_write_png(tmp_png.c_str(), w, h, channels, pixels.data(), w * channels);
    REQUIRE(ok != 0);

    // Read the PNG back as bytes
    std::ifstream f(tmp_png, std::ios::binary);
    std::string input_png((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    f.close();
    std::filesystem::remove(tmp_png);

    REQUIRE(!input_png.empty());

    // Optimize it as PNG (won't resize since 4px < 1920)
    int saved = 0;
    std::string result = optimize_image(input_png, 1920, 85, ".png", saved);
    REQUIRE(!result.empty());

    // Verify the result is a valid PNG
    int out_w, out_h, out_ch;
    unsigned char* decoded = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(result.data()),
        static_cast<int>(result.size()), &out_w, &out_h, &out_ch, 0);
    REQUIRE(decoded != nullptr);
    CHECK(out_w == 4);
    CHECK(out_h == 4);
    stbi_image_free(decoded);
}

TEST_CASE("Image: optimize_image resizes wide images", "[image]") {
    // Create a 100x50 image and write to temp PNG
    int w = 100, h = 50, channels = 3;
    std::vector<unsigned char> pixels(w * h * channels, 128);

    std::string tmp_png = "/tmp/cstatic_test_wide.png";
    int ok = stbi_write_png(tmp_png.c_str(), w, h, channels, pixels.data(), w * channels);
    REQUIRE(ok != 0);

    std::ifstream f(tmp_png, std::ios::binary);
    std::string input_png((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    f.close();
    std::filesystem::remove(tmp_png);

    // Optimize with max_width=50 — should resize to 50x25
    int saved = 0;
    std::string result = optimize_image(input_png, 50, 85, ".png", saved);
    REQUIRE(!result.empty());

    int out_w, out_h, out_ch;
    unsigned char* decoded = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(result.data()),
        static_cast<int>(result.size()), &out_w, &out_h, &out_ch, 0);
    REQUIRE(decoded != nullptr);
    CHECK(out_w == 50);
    CHECK(out_h == 25);
    stbi_image_free(decoded);
}

// --- Fingerprint Tests ---

TEST_CASE("Fingerprint: fingerprint_path produces stem.hash.ext format", "[fingerprint]") {
    std::string result = AssetManifest::fingerprint_path("css/style.css", "a3f7b2c1");
    REQUIRE(result == "css/style.a3f7b2c1.css");
}

TEST_CASE("Fingerprint: fingerprint_path handles multi-segment paths", "[fingerprint]") {
    std::string result = AssetManifest::fingerprint_path("js/vendor/app.min.js", "e5d4f3a2");
    REQUIRE(result == "js/vendor/app.min.e5d4f3a2.js");
}

TEST_CASE("Fingerprint: fingerprint_path handles no extension", "[fingerprint]") {
    std::string result = AssetManifest::fingerprint_path("Makefile", "abcdef01");
    REQUIRE(result == "Makefile.abcdef01");
}

TEST_CASE("Fingerprint: build_asset_manifest creates manifest from static dir", "[fingerprint]") {
    // Create a temp static directory with a test file
    std::string tmp_dir = "/tmp/cstatic_test_manifest";
    std::filesystem::remove_all(tmp_dir);
    std::filesystem::create_directories(tmp_dir + "/css");

    // Write a test CSS file
    std::ofstream f(tmp_dir + "/css/style.css");
    f << "body { color: red; }";
    f.close();

    Config cfg;
    cfg.static_dir = tmp_dir;
    cfg.output_dir = "/tmp/cstatic_test_output_manifest";

    auto manifest = build_asset_manifest(cfg);
    REQUIRE(manifest.entries.count("css/style.css") == 1);

    std::string fp = manifest.entries["css/style.css"];
    REQUIRE(fp.find("css/style.") == 0);
    REQUIRE(fp.find(".css") == fp.size() - 4);
    // The hash portion should be 8 chars
    // "css/style." is 10 chars, ".css" is 4, so hash is fp[10..18]
    REQUIRE(fp.size() == 10 + 8 + 4);

    std::filesystem::remove_all(tmp_dir);
}

// --- Asset Template Helper Test ---

TEST_CASE("Fingerprint: asset() callback resolves correctly", "[template]") {
    // Create a minimal template dir
    std::string tmp_dir = "/tmp/cstatic_test_renderer";
    std::filesystem::remove_all(tmp_dir);
    std::filesystem::create_directories(tmp_dir);

    // Write a template that uses asset()
    std::ofstream f(tmp_dir + "/test.html");
    f << "<link href=\"{{ asset(\"css/style.css\") }}\">";
    f.close();

    TemplateRenderer renderer(tmp_dir);

    std::map<std::string, std::string> manifest;
    manifest["css/style.css"] = "css/style.a3f7b2c1.css";
    renderer.set_asset_manifest(manifest);

    nlohmann::json data;
    data["site"] = nlohmann::json::object();
    data["page"] = nlohmann::json::object();

    std::string result = renderer.render("test", data);
    REQUIRE(result.find("css/style.a3f7b2c1.css") != std::string::npos);

    std::filesystem::remove_all(tmp_dir);
}

TEST_CASE("Fingerprint: asset() falls back to original when not in manifest", "[template]") {
    std::string tmp_dir = "/tmp/cstatic_test_renderer_fallback";
    std::filesystem::remove_all(tmp_dir);
    std::filesystem::create_directories(tmp_dir);

    std::ofstream f(tmp_dir + "/test.html");
    f << "<link href=\"{{ asset(\"js/app.js\") }}\">";
    f.close();

    TemplateRenderer renderer(tmp_dir);

    // Empty manifest — asset() should return original path
    std::map<std::string, std::string> manifest;
    renderer.set_asset_manifest(manifest);

    nlohmann::json data;
    data["site"] = nlohmann::json::object();
    data["page"] = nlohmann::json::object();

    std::string result = renderer.render("test", data);
    REQUIRE(result.find("js/app.js") != std::string::npos);

    std::filesystem::remove_all(tmp_dir);
}
