#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace cstatic {

// Content-hash based cache for incremental builds.
// Uses XXH64 hashing. Persists hashes to a JSON file with atomic writes.
class HashStore {
public:
    explicit HashStore(const std::string& hash_file);

    // Load previously stored hashes from disk.
    // Returns true if the file existed and was loaded successfully.
    bool load();

    // Save current hashes to disk (atomic: write to .tmp then rename).
    void save() const;

    // Compute and store the hash for a file's contents.
    void hash_file(const std::string& path);

    // Compute and store the hash for an arbitrary string (e.g. config contents).
    void hash_string(const std::string& key, const std::string& contents);

    // Check if a file's current content matches its stored hash.
    // Returns true if the file hash has NOT changed (cache hit).
    bool is_unchanged(const std::string& path) const;

    // Check if a key's current content matches its stored hash.
    bool is_unchanged_key(const std::string& key) const;

    // Prune stored hashes for files that no longer exist on disk.
    void prune_deleted(const std::vector<std::string>& active_paths);

    // Get all stored hash keys (for debugging).
    const std::unordered_map<std::string, uint64_t>& current_hashes() const {
        return current_;
    }

private:
    std::string hash_file_;
    std::unordered_map<std::string, uint64_t> previous_;  // loaded from disk
    std::unordered_map<std::string, uint64_t> current_;    // computed this run

    // Compute XXH64 of a string.
    static uint64_t compute_hash(const std::string& data);
};

} // namespace cstatic
