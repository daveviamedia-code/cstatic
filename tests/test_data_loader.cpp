#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "data/data_loader.hpp"

namespace fs = std::filesystem;

using namespace cstatic;

struct DataFixture {
    std::string data_dir;
    DataFixture() : data_dir("test_data") {
        fs::create_directories(data_dir);
    }
    ~DataFixture() {
        fs::remove_all(data_dir);
    }

    void write_data(const std::string& name, const std::string& content) {
        std::ofstream f(data_dir + "/" + name);
        f << content;
    }
};

TEST_CASE_METHOD(DataFixture, "DataLoader: JSON loading", "[data]") {
    write_data("products.json", R"([
        {"name": "Widget", "price": 9.99},
        {"name": "Gadget", "price": 19.99}
    ])");

    DataLoader loader(data_dir);
    auto data = loader.load_all();
    REQUIRE(data.contains("products"));
    REQUIRE(data["products"].is_array());
    REQUIRE(data["products"].size() == 2);
    REQUIRE(data["products"][0]["name"] == "Widget");
}

TEST_CASE_METHOD(DataFixture, "DataLoader: YAML loading", "[data]") {
    write_data("items.yaml", R"(
- name: Alpha
  count: 10
- name: Beta
  count: 20
)");

    DataLoader loader(data_dir);
    auto data = loader.load_all();
    REQUIRE(data.contains("items"));
    REQUIRE(data["items"].is_array());
    REQUIRE(data["items"].size() == 2);
    REQUIRE(data["items"][0]["name"] == "Alpha");
    REQUIRE(data["items"][0]["count"] == 10);
}

TEST_CASE_METHOD(DataFixture, "DataLoader: missing data directory", "[data]") {
    DataLoader loader("nonexistent_data_dir");
    auto data = loader.load_all();
    REQUIRE(data.is_object());
    REQUIRE(data.empty());
}

TEST_CASE_METHOD(DataFixture, "DataLoader: mixed JSON and YAML", "[data]") {
    write_data("config.json", R"({"theme": "dark"})");
    write_data("authors.yaml", R"(name: Jane)");

    DataLoader loader(data_dir);
    auto data = loader.load_all();
    REQUIRE(data.contains("config"));
    REQUIRE(data.contains("authors"));
    REQUIRE(data["config"]["theme"] == "dark");
    REQUIRE(data["authors"]["name"] == "Jane");
}

TEST_CASE_METHOD(DataFixture, "DataLoader: invalid JSON throws", "[data]") {
    write_data("bad.json", "{invalid json}");

    DataLoader loader(data_dir);
    REQUIRE_THROWS_AS(loader.load_all(), std::runtime_error);
}

TEST_CASE_METHOD(DataFixture, "DataLoader: invalid YAML throws", "[data]") {
    write_data("bad.yaml", "key: [bad\nyaml");

    DataLoader loader(data_dir);
    REQUIRE_THROWS_AS(loader.load_all(), std::runtime_error);
}

TEST_CASE_METHOD(DataFixture, "DataLoader: nested path becomes key", "[data]") {
    fs::create_directories(data_dir + "/sub");
    write_data("sub/nested.json", R"({"value": 42})");

    DataLoader loader(data_dir);
    auto data = loader.load_all();
    REQUIRE(data.contains("sub/nested"));
    REQUIRE(data["sub/nested"]["value"] == 42);
}

TEST_CASE_METHOD(DataFixture, "DataLoader: YAML type conversion", "[data]") {
    write_data("types.yaml", R"(
string_val: hello
int_val: 42
float_val: 3.14
bool_true: true
bool_false: false
)");

    DataLoader loader(data_dir);
    auto data = loader.load_all();
    REQUIRE(data["types"]["string_val"] == "hello");
    REQUIRE(data["types"]["int_val"] == 42);
    REQUIRE(data["types"]["bool_true"] == true);
    REQUIRE(data["types"]["bool_false"] == false);
}
