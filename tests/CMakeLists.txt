include(FetchContent)

# Catch2 v3.5.0
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.0
)
FetchContent_MakeAvailable(Catch2)

# Library source files (everything except main.cpp)
set(LIB_SOURCES
    ${CMAKE_SOURCE_DIR}/src/assets/asset_pipeline.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/content_generator.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/error_format.cpp
    ${CMAKE_SOURCE_DIR}/src/config/config.cpp
    ${CMAKE_SOURCE_DIR}/src/content/authors_index.cpp
    ${CMAKE_SOURCE_DIR}/src/content/frontmatter.cpp
    ${CMAKE_SOURCE_DIR}/src/content/link_graph.cpp
    ${CMAKE_SOURCE_DIR}/src/content/markdown.cpp
    ${CMAKE_SOURCE_DIR}/src/content/passage_index.cpp
    ${CMAKE_SOURCE_DIR}/src/content/schema_blocks.cpp
    ${CMAKE_SOURCE_DIR}/src/content/shortcodes.cpp
    ${CMAKE_SOURCE_DIR}/src/data/data_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/hash/hash_store.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/sitemap.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/rss.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/json_feed.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/llms_txt.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/robots.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/seo_schema.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/search.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/og_images.cpp
    ${CMAKE_SOURCE_DIR}/src/server/file_watcher.cpp
    ${CMAKE_SOURCE_DIR}/src/stb_impl.cpp
    ${CMAKE_SOURCE_DIR}/src/template/renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/builder.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/link_checker.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/file_io.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/path.cpp
)

# Test source files
set(TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/tests/test_config.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_frontmatter.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_markdown.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_path_utils.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_hash_store.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_template_renderer.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_minifier.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_data_loader.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_build_integration.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_shortcodes.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_content_generator.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_link_checker.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_link_graph.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_passage_index.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_error_format.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_file_watcher.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_robots.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_llms_txt.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_seo_schema.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_schema_blocks.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_authors_index.cpp
)

add_executable(cstatic_tests ${TEST_SOURCES} ${LIB_SOURCES})

target_include_directories(cstatic_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${stb_SOURCE_DIR}
    ${cmark-gfm_SOURCE_DIR}/src
    ${cmark-gfm_SOURCE_DIR}/extensions
    ${cmark-gfm_BINARY_DIR}/src
)

target_link_libraries(cstatic_tests PRIVATE
    Catch2::Catch2WithMain
    CLI11::CLI11
    tomlplusplus::tomlplusplus
    yaml-cpp::yaml-cpp
    libcmark-gfm-extensions_static
    libcmark-gfm_static
    inja
    nlohmann_json
    xxHash::xxhash
    httplib::httplib
)

# Register tests with CTest
include(CTest)
include(Catch)
catch_discover_tests(cstatic_tests)

# --- Benchmarks (opt-in via CSTATIC_BUILD_BENCHMARKS) ---
option(CSTATIC_BUILD_BENCHMARKS "Build benchmarks" OFF)
if(CSTATIC_BUILD_BENCHMARKS)
    add_executable(cstatic_bench
        ${CMAKE_SOURCE_DIR}/tests/bench_build.cpp
        ${LIB_SOURCES}
    )
    target_include_directories(cstatic_bench PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
        ${cmark-gfm_SOURCE_DIR}/src
        ${cmark-gfm_SOURCE_DIR}/extensions
        ${cmark-gfm_BINARY_DIR}/src
    )
    target_link_libraries(cstatic_bench PRIVATE
        Catch2::Catch2WithMain
        CLI11::CLI11
        tomlplusplus::tomlplusplus
        yaml-cpp::yaml-cpp
        libcmark-gfm_static
        libcmark-gfm-extensions_static
        inja
        nlohmann_json
        xxHash::xxhash
        httplib::httplib
    )
endif()
