#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <cstdlib>

#include "content/shortcodes.hpp"
#include "utils/file_io.hpp"
#include "test_util.hpp"

namespace fs = std::filesystem;

using namespace cstatic;

namespace {

// Per-test temp directory containing a shortcodes/ folder. Tests write
// templates into it then construct a ShortcodeProcessor pointed at it.
struct ShortcodeFixture {
    std::string root_dir;
    std::string saved_cwd;

    ShortcodeFixture() {
        saved_cwd = fs::current_path().string();
        root_dir = cstatic_test::unique_temp_dir("cstatic_sc_");
        fs::create_directories(root_dir);
        fs::create_directories(root_dir + "/shortcodes");
        fs::current_path(root_dir);
    }

    ~ShortcodeFixture() {
        fs::current_path(saved_cwd);
        fs::remove_all(root_dir);
    }

    // Write a template file at shortcodes/<name>.html.
    void write_template(const std::string& name, const std::string& content) {
        std::string path = root_dir + "/shortcodes/" + name + ".html";
        std::ofstream f(path);
        f << content;
        f.close();
        REQUIRE(f.good());
    }
};

} // anonymous namespace

TEST_CASE("Shortcodes: disabled when directory missing", "[shortcodes]") {
    ShortcodeProcessor sc("definitely-does-not-exist-xyz");
    REQUIRE_FALSE(sc.available());
    // process() on unavailable processor is a no-op pass-through.
    REQUIRE(sc.process("hello {{< youtube x >}} world") ==
            "hello {{< youtube x >}} world");
}

TEST_CASE("Shortcodes: inline positional param", "[shortcodes]") {
    ShortcodeFixture fx;
    fx.write_template("echo", "[[{{ params.0 }}]]");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    REQUIRE(sc.available());
    REQUIRE(sc.process("a {{< echo hello >}} b") == "a [[hello]] b");
}

TEST_CASE("Shortcodes: inline with multiple positional params", "[shortcodes]") {
    ShortcodeFixture fx;
    fx.write_template("two", "{{ params.0 }}+{{ params.1 }}");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    REQUIRE(sc.process("{{< two foo bar >}}") == "foo+bar");
}

TEST_CASE("Shortcodes: named params", "[shortcodes]") {
    ShortcodeFixture fx;
    fx.write_template("img",
        R"(<img src="{{ named.src }}" alt="{{ named.alt }}">)");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    std::string out = sc.process(
        "{{< img src=\"/x.jpg\" alt=\"A cat\" >}}");
    REQUIRE(out.find("src=\"/x.jpg\"") != std::string::npos);
    REQUIRE(out.find("alt=\"A cat\"") != std::string::npos);
}

TEST_CASE("Shortcodes: single-quoted named params", "[shortcodes]") {
    ShortcodeFixture fx;
    fx.write_template("img", "<img src='{{ named.src }}'>");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    std::string out = sc.process("{{< img src='/y.png' >}}");
    REQUIRE(out.find("src='/y.png'") != std::string::npos);
}

TEST_CASE("Shortcodes: block shortcode captures inner content", "[shortcodes]") {
    ShortcodeFixture fx;
    fx.write_template("wrap", "BEGIN[{{ content }}]END");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    REQUIRE(sc.process("{{< wrap >}}hello there{{< /wrap >}}") ==
            "BEGIN[hello there]END");
}

TEST_CASE("Shortcodes: block with flexible whitespace", "[shortcodes]") {
    ShortcodeFixture fx;
    fx.write_template("b", "<b>{{ content }}</b>");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    // Tight close: {{</b>}}
    REQUIRE(sc.process("{{<b>}}x{{</b>}}") == "<b>x</b>");
    // Spaced close: {{< /b >}}
    REQUIRE(sc.process("{{< b >}}x{{< /b >}}") == "<b>x</b>");
}

TEST_CASE("Shortcodes: multiple shortcodes in one document", "[shortcodes]") {
    ShortcodeFixture fx;
    fx.write_template("a", "A({{ params.0 }})");
    fx.write_template("b", "B({{ params.0 }})");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    std::string out = sc.process("pre {{< a 1 >}} mid {{< b 2 >}} post");
    REQUIRE(out == "pre A(1) mid B(2) post");
}

TEST_CASE("Shortcodes: mixed inline and block", "[shortcodes]") {
    ShortcodeFixture fx;
    fx.write_template("box", "[BOX:{{ content }}]");
    fx.write_template("tag", "#{{ params.0 }}#");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    std::string out = sc.process(
        "{{< box >}}before {{< tag x >}} after{{< /box >}}");
    REQUIRE(out == "[BOX:before #x# after]");
}

TEST_CASE("Shortcodes: nested blocks of same type", "[shortcodes]") {
    ShortcodeFixture fx;
    // Wrap block whose inner content itself contains a wrap block — the
    // closest-close match should win, leaving the outer wrap intact.
    fx.write_template("w", "({{ content }})");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    // Outer block closes at the second /w marker.
    std::string out = sc.process("{{< w >}}outer{{< w >}}inner{{< /w >}}tail{{< /w >}}");
    // Inner expands first to "(inner)", then outer to "(outer(inner)tail)".
    REQUIRE(out == "(outer(inner)tail)");
}

TEST_CASE("Shortcodes: unknown shortcode passes through empty", "[shortcodes]") {
    ShortcodeFixture fx;
    fx.write_template("known", "ok");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    // Unknown template -> render_one returns "" (after stderr warning).
    // The original tag text is NOT reinserted; callers see the slot vanish.
    std::string out = sc.process("a {{< nope x >}} b");
    REQUIRE(out == "a  b");
}

TEST_CASE("Shortcodes: stray closing tag passes through", "[shortcodes]") {
    ShortcodeFixture fx;
    fx.write_template("x", "X");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    // A closing tag with no matching opener should be left in place, not
    // crash, not silently vanish.
    std::string out = sc.process("text {{< /orphan >}} tail");
    REQUIRE(out.find("{{< /orphan >}}") != std::string::npos);
}

TEST_CASE("Shortcodes: no shortcodes leaves text untouched", "[shortcodes]") {
    ShortcodeFixture fx;
    fx.write_template("x", "X");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    REQUIRE(sc.process("plain markdown\n\nwith no tags") ==
            "plain markdown\n\nwith no tags");
}

TEST_CASE("Shortcodes: page context available to templates", "[shortcodes]") {
    ShortcodeFixture fx;
    fx.write_template("ref", "{{ page.title }} @ {{ page.url }}");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    nlohmann::json page;
    page["title"] = "Hello";
    page["url"]   = "/posts/hello/";
    std::string out = sc.process("{{< ref >}}", page);
    REQUIRE(out == "Hello @ /posts/hello/");
}

TEST_CASE("Shortcodes: empty directory makes processor unavailable",
          "[shortcodes]") {
    ShortcodeFixture fx;  // creates shortcodes/ but writes no templates
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    REQUIRE_FALSE(sc.available());
    // process() should pass through unchanged.
    REQUIRE(sc.process("nothing {{< to >}} see") == "nothing {{< to >}} see");
}

TEST_CASE("Shortcodes: template cached across calls", "[shortcodes]") {
    ShortcodeFixture fx;
    fx.write_template("c", "{{ params.0 }}");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    REQUIRE(sc.process("{{< c one >}}") == "one");
    // Overwrite the file on disk; cached version should still win.
    fx.write_template("c", "CHANGED");
    REQUIRE(sc.process("{{< c two >}}") == "two");
}

TEST_CASE("Shortcodes: empty body and empty shortcode", "[shortcodes]") {
    ShortcodeFixture fx;
    fx.write_template("x", "X");
    ShortcodeProcessor sc(fx.root_dir + "/shortcodes");
    REQUIRE(sc.process("").empty());
    // Empty {{< >}} should be left as-is (no name to dispatch on).
    REQUIRE(sc.process("a {{< >}} b") == "a {{< >}} b");
}
