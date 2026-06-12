#include <catch2/catch_test_macros.hpp>
#include "assets/asset_pipeline.hpp"

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
