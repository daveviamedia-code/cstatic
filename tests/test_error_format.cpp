#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "cli/error_format.hpp"
#include "pipeline/builder.hpp"
#include "test_util.hpp"

namespace fs = std::filesystem;

using cstatic::BuildError;
using cstatic::cli::format_build_error;

namespace {

// RAII temp dir with a single source file + template; returns paths by reference.
struct TempProject {
    std::string root;
    std::string template_dir;
    std::string src_file;

    TempProject() {
        root = cstatic_test::unique_temp_dir("cstatic_efmt_");
        fs::create_directories(root + "/templates");
        template_dir = root + "/templates";
    }

    ~TempProject() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // Write a source file under root and remember its path.
    void write_source(const std::string& name, const std::string& content) {
        src_file = root + "/" + name;
        std::ofstream(src_file) << content;
    }

    void write_template(const std::string& name, const std::string& content) {
        std::ofstream(root + "/templates/" + name + ".html") << content;
    }
};

} // namespace

TEST_CASE("error_format: frontmatter error shows source context and caret", "[error_format]") {
    TempProject tp;
    // File layout:
    //   line 1: ---
    //   line 2: title: T
    //   line 3: data: {bad
    //   line 4: ---
    //   line 5: body
    tp.write_source("broken.md", "---\ntitle: T\ndata: {bad\n---\nbody\n");

    BuildError err;
    err.type = BuildError::Type::Frontmatter;
    err.source_file = tp.src_file;
    err.line = 3;
    err.column = 7;  // the '{' on "data: {bad"

    std::string out = format_build_error(err, tp.template_dir);

    // The offending source line is present.
    REQUIRE(out.find("data: {bad") != std::string::npos);
    // Gutter marks line 3 with '>'.
    REQUIRE(out.find("> 3 |") != std::string::npos);
    // A caret is rendered.
    REQUIRE(out.find("^") != std::string::npos);
}

TEST_CASE("error_format: line==0 produces no context gutter", "[error_format]") {
    TempProject tp;
    tp.write_source("ok.md", "---\ntitle: T\n---\nbody\n");

    BuildError err;
    err.type = BuildError::Type::Frontmatter;
    err.source_file = tp.src_file;
    err.line = 0;
    err.column = 0;
    err.message = "something went wrong";

    std::string out = format_build_error(err, tp.template_dir);

    // No '>' gutter marker when there's no line info.
    REQUIRE(out.find("> ") == std::string::npos);
    REQUIRE(out.find("|") == std::string::npos);
    // Message is still present.
    REQUIRE(out.find("something went wrong") != std::string::npos);
}

TEST_CASE("error_format: template error reads template file for context", "[error_format]") {
    TempProject tp;
    // Template with content on multiple lines so context is non-empty.
    std::string tmpl = "<html>\n<body>\n{{ page.content }}\n{{ undefined.thing }}\n</body>\n</html>\n";
    tp.write_template("default", tmpl);

    BuildError err;
    err.type = BuildError::Type::Template;
    err.source_file = "src/index.md";
    err.template_name = "default";
    err.line = 4;
    err.column = 0;
    err.message = "undefined variable";

    std::string out = format_build_error(err, tp.template_dir);

    // Template content from the offending line is shown.
    REQUIRE(out.find("undefined.thing") != std::string::npos);
    // Gutter marks line 4.
    REQUIRE(out.find("> 4 |") != std::string::npos);
}
