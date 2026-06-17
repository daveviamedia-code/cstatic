cmake_minimum_required(VERSION 3.16)
project(cstatic VERSION 0.2.0 LANGUAGES CXX)

# Policy override for dependency compatibility (yaml-cpp requires older CMake minimum)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE INTERNAL "")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies via FetchContent ---
include(FetchContent)

# CLI11 — CLI argument parsing
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2
)

# toml++ — TOML config parsing (header-only)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
)

# yaml-cpp — YAML frontmatter parsing
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
)

# cmark-gfm — GitHub-Flavored Markdown to HTML (tables, task lists, strikethrough)
FetchContent_Declare(
    cmark-gfm
    GIT_REPOSITORY https://github.com/github/cmark-gfm.git
    GIT_TAG        0.29.0.gfm.13
)

# inja — Template engine (header-only)
# Brings its own embedded nlohmann/json — no separate FetchContent needed
FetchContent_Declare(
    inja
    GIT_REPOSITORY https://github.com/pantor/inja.git
    GIT_TAG        v3.4.0
)

# xxHash — fast non-cryptographic hashing
FetchContent_Declare(
    xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG        v0.8.2
    SOURCE_SUBDIR  cmake_unofficial
)

# cpp-httplib — HTTP server for dev serve command (header-only)
FetchContent_Declare(
    cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.15.3
)

# stb — image loading/writing (header-only, from nothings/stb)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)

# --- Dependency options (set before MakeAvailable) ---
# Disable tests in dependencies (not our own tests)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INJA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INJA_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(CMARK_TESTS OFF CACHE BOOL "" FORCE)
set(CMARK_SHARED OFF CACHE BOOL "" FORCE)
set(CMARK_STATIC ON CACHE BOOL "" FORCE)

# cpp-httplib — we only use it for local dev server, no HTTPS needed
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cli11 tomlplusplus yaml-cpp cmark-gfm inja xxhash cpp-httplib stb)

# --- Main executable ---
add_executable(cstatic
    src/main.cpp
    src/assets/asset_pipeline.cpp
    src/config/config.cpp
    src/content/frontmatter.cpp
    src/content/markdown.cpp
    src/content/shortcodes.cpp
    src/data/data_loader.cpp
    src/hash/hash_store.cpp
    src/modules/sitemap.cpp
    src/modules/rss.cpp
    src/modules/robots.cpp
    src/modules/search.cpp
    src/modules/og_images.cpp
    src/server/dev_server.cpp
    src/stb_impl.cpp
    src/template/renderer.cpp
    src/pipeline/builder.cpp
    src/utils/file_io.cpp
    src/utils/path.cpp
)

target_include_directories(cstatic PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${stb_SOURCE_DIR}
    ${cmark-gfm_SOURCE_DIR}/src
    ${cmark-gfm_SOURCE_DIR}/extensions
    ${cmark-gfm_BINARY_DIR}/src
)

target_link_libraries(cstatic PRIVATE
    CLI11::CLI11
    tomlplusplus::tomlplusplus
    yaml-cpp::yaml-cpp
    libcmark-gfm_static
    libcmark-gfm-extensions_static
    inja                 # inja creates target 'inja' (alias: pantor::inja)
    nlohmann_json        # provided by inja's embedded copy
    xxHash::xxhash
    httplib::httplib
)

# Embed version from CMake into binary
target_compile_definitions(cstatic PRIVATE
    CSTATIC_VERSION="${PROJECT_VERSION}"
)

# --- Release optimizations ---
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(cstatic PRIVATE -O2)
    if(APPLE)
        set_target_properties(cstatic PROPERTIES
            MACOSX_RPATH ON
        )
    endif()
    # Strip debug symbols for release
    if(NOT WIN32)
        target_compile_options(cstatic PRIVATE -DNDEBUG)
        set_target_properties(cstatic PROPERTIES
            LINK_FLAGS "-s"
        )
    endif()
endif()

# --- Static linking on Linux ---
if(UNIX AND NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()

# --- Fully static binary option ---
option(CSTATIC_STATIC "Build fully static binary" OFF)
if(CSTATIC_STATIC)
    if(UNIX AND NOT APPLE)
        # Linux: full static linking including libc
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
        target_compile_options(cstatic PRIVATE -static)
    elseif(WIN32)
        # Windows (MSVC): link static CRT (/MT) so no VC++ Redistributable needed
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    endif()
    # macOS: no effect — Apple does not support fully static executables
endif()

# --- Install target ---
include(GNUInstallDirs)
install(TARGETS cstatic
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cstatic
    FILES_MATCHING PATTERN "*.hpp"
)

# --- Tests (only when CSTATIC_BUILD_TESTS is ON) ---
option(CSTATIC_BUILD_TESTS "Build C-Static test suite" OFF)
if(CSTATIC_BUILD_TESTS)
    enable_testing()
    set(BUILD_TESTING ON)
    add_subdirectory(tests)
endif()
