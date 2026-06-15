#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "template/renderer.hpp"

namespace fs = std::filesystem;

using namespace cstatic;

struct TemplateFixture {
    std::string template_dir;
    TemplateFixture() : template_dir("test_templates") {
        fs::create_directories(template_dir);
        fs::create_directories(template_dir + "/partials");
    }
    ~TemplateFixture() {
        fs::remove_all(template_dir);
    }

    void write_template(const std::string& name, const std::string& content) {
        std::ofstream f(template_dir + "/" + name);
        f << content;
    }

    void write_partial(const std::string& name, const std::string& content) {
        std::ofstream f(template_dir + "/partials/" + name + ".html");
        f << content;
    }
};

TEST_CASE_METHOD(TemplateFixture, "Renderer: basic substitution", "[template]") {
    write_template("default.html",
        "<h1>{{ page.title }}</h1>\n<div>{{ page.content }}</div>");

    TemplateRenderer renderer(template_dir);
    nlohmann::json data;
    data["page"]["title"] = "Hello";
    data["page"]["content"] = "<p>World</p>";

    std::string html = renderer.render("default", data);
    REQUIRE(html.find("<h1>Hello</h1>") != std::string::npos);
    REQUIRE(html.find("<div><p>World</p></div>") != std::string::npos);
}

TEST_CASE_METHOD(TemplateFixture, "Renderer: site context", "[template]") {
    write_template("default.html",
        "<title>{{ site.title }} — {{ page.title }}</title>");

    TemplateRenderer renderer(template_dir);
    nlohmann::json data;
    data["site"]["title"] = "My Site";
    data["page"]["title"] = "About";

    std::string html = renderer.render("default", data);
    REQUIRE(html.find("<title>My Site — About</title>") != std::string::npos);
}

TEST_CASE_METHOD(TemplateFixture, "Renderer: missing template uses fallback", "[template]") {
    TemplateRenderer renderer(template_dir);
    nlohmann::json data;
    data["site"]["language"] = "en";
    data["page"]["title"] = "Fallback Test";
    data["page"]["content"] = "<p>Fallback content</p>";

    // "nonexistent" template doesn't exist — should use built-in fallback
    std::string html = renderer.render("nonexistent", data);
    REQUIRE(html.find("Fallback content") != std::string::npos);
    REQUIRE(html.find("<!DOCTYPE html>") != std::string::npos);
}

TEST_CASE_METHOD(TemplateFixture, "Renderer: inja error throws RenderError with context", "[template]") {
    write_template("bad.html", "<h1>Line 1</h1>\n<h2>Line 2</h2>\n{{ undefined_var.nonexistent }}");

    TemplateRenderer renderer(template_dir);
    nlohmann::json data;

    REQUIRE_THROWS_AS(renderer.render("bad", data, "src/test.md"), cstatic::RenderError);
    try {
        renderer.render("bad", data, "src/test.md");
    } catch (const cstatic::RenderError& e) {
        REQUIRE(e.template_name() == "bad");
        REQUIRE(e.source_file() == "src/test.md");
        REQUIRE(e.line() > 0);
        REQUIRE(std::string(e.what()).find("undefined_var") != std::string::npos);
    }
}

TEST_CASE_METHOD(TemplateFixture, "Renderer: loop over pages", "[template]") {
    write_template("default.html",
        "{% for p in pages %}<a href=\"{{ p.url }}\">{{ p.title }}</a>{% endfor %}");

    TemplateRenderer renderer(template_dir);
    nlohmann::json data;
    data["pages"] = nlohmann::json::array();
    data["pages"].push_back({{"url", "/a/"}, {"title", "A"}});
    data["pages"].push_back({{"url", "/b/"}, {"title", "B"}});

    std::string html = renderer.render("default", data);
    REQUIRE(html.find("<a href=\"/a/\">A</a>") != std::string::npos);
    REQUIRE(html.find("<a href=\"/b/\">B</a>") != std::string::npos);
}

TEST_CASE_METHOD(TemplateFixture, "Renderer: include partial", "[template]") {
    write_partial("nav", "<nav>{{ site.title }}</nav>");
    write_template("default.html",
        "{% include \"nav\" %}<main>{{ page.content }}</main>");

    TemplateRenderer renderer(template_dir);
    nlohmann::json data;
    data["site"]["title"] = "My Site";
    data["page"]["content"] = "Hello";

    std::string html = renderer.render("default", data);
    REQUIRE(html.find("<nav>My Site</nav>") != std::string::npos);
    REQUIRE(html.find("<main>Hello</main>") != std::string::npos);
}
