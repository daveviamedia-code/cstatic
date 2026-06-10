#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "hash/hash_store.hpp"

namespace fs = std::filesystem;

using namespace cstatic;

struct HashFixture {
    std::string hash_file;
    HashFixture() : hash_file("test_hashes.json") {}
    ~HashFixture() {
        fs::remove(hash_file);
        fs::remove(hash_file + ".tmp");
    }
};

TEST_CASE_METHOD(HashFixture, "HashStore: compute hash is deterministic", "[hash]") {
    HashStore store(hash_file);
    // hash_string is private, but we can test via load/save round-trip
    // We test the file hashing path
    std::string test_file = "test_hash_input.txt";
    {
        std::ofstream f(test_file);
        f << "hello world";
    }

    store.hash_file(test_file);
    auto hashes = store.current_hashes();
    REQUIRE(hashes.size() == 1);

    // Hash same file again — should produce same value
    HashStore store2(hash_file);
    store2.hash_file(test_file);
    auto hashes2 = store2.current_hashes();
    REQUIRE(hashes2.size() == 1);
    REQUIRE(hashes.begin()->second == hashes2.begin()->second);

    fs::remove(test_file);
}

TEST_CASE_METHOD(HashFixture, "HashStore: load/save round-trip", "[hash]") {
    // Create a temp file to hash
    std::string test_file = "test_roundtrip.txt";
    {
        std::ofstream f(test_file);
        f << "test content for round-trip";
    }

    // Store and save
    {
        HashStore store(hash_file);
        store.hash_file(test_file);
        store.save();
    }

    // Load and verify
    {
        HashStore store(hash_file);
        REQUIRE(store.load());
        store.hash_file(test_file);
        REQUIRE(store.is_unchanged(test_file));
    }

    fs::remove(test_file);
}

TEST_CASE_METHOD(HashFixture, "HashStore: unchanged detection", "[hash]") {
    std::string test_file = "test_unchanged.txt";
    {
        std::ofstream f(test_file);
        f << "original content";
    }

    // First run: hash and save
    {
        HashStore store(hash_file);
        store.hash_file(test_file);
        store.save();
    }

    // Second run: same content — should be unchanged
    {
        HashStore store(hash_file);
        store.load();
        store.hash_file(test_file);
        REQUIRE(store.is_unchanged(test_file));
    }

    // Modify file
    {
        std::ofstream f(test_file);
        f << "modified content";
    }

    // Third run: changed — should NOT be unchanged
    {
        HashStore store(hash_file);
        store.load();
        store.hash_file(test_file);
        REQUIRE_FALSE(store.is_unchanged(test_file));
    }

    fs::remove(test_file);
}

TEST_CASE_METHOD(HashFixture, "HashStore: new file is not unchanged", "[hash]") {
    HashStore store(hash_file);
    // No previous hashes loaded — any file should be "changed"
    std::string test_file = "test_new_file.txt";
    {
        std::ofstream f(test_file);
        f << "new file";
    }

    store.hash_file(test_file);
    REQUIRE_FALSE(store.is_unchanged(test_file));

    fs::remove(test_file);
}

TEST_CASE_METHOD(HashFixture, "HashStore: prune removes deleted entries", "[hash]") {
    std::string test_file = "test_prune.txt";
    {
        std::ofstream f(test_file);
        f << "content";
    }

    // Save with a file entry
    {
        HashStore store(hash_file);
        store.hash_file(test_file);
        store.save();
    }

    // Load with empty active list — prune the entry and save
    {
        HashStore store(hash_file);
        store.load();
        // Don't hash the file again — just prune its previous entry
        std::vector<std::string> active; // empty = prune everything
        store.prune_deleted(active);
        store.save();
    }

    // Load again — the previous hash entry should be gone, so file is "changed"
    {
        HashStore store(hash_file);
        store.load();
        store.hash_file(test_file);
        REQUIRE_FALSE(store.is_unchanged(test_file));
    }

    fs::remove(test_file);
}

TEST_CASE_METHOD(HashFixture, "HashStore: load nonexistent file returns false", "[hash]") {
    HashStore store("nonexistent_hash_file.json");
    REQUIRE_FALSE(store.load());
}
