#include "hash/hash_store.hpp"
#include "utils/path.hpp"

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>

namespace cstatic {

namespace fs = std::filesystem;

HashStore::HashStore(const std::string& hash_file)
    : hash_file_(hash_file) {}

bool HashStore::load() {
    previous_.clear();

    if (!fs::exists(hash_file_)) return false;

    std::ifstream f(hash_file_);
    if (!f.is_open()) return false;

    try {
        nlohmann::json j;
        f >> j;

        if (!j.is_object()) return false;

        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.value().is_string()) {
                // Parse hex string to uint64_t
                uint64_t hash = std::stoull(it.value().get<std::string>(), nullptr, 16);
                previous_[it.key()] = hash;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

void HashStore::save() const {
    nlohmann::json j = nlohmann::json::object();

    for (const auto& [key, hash] : current_) {
        // Store as hex string for readability
        std::ostringstream oss;
        oss << std::hex << std::setw(16) << std::setfill('0') << hash;
        j[key] = oss.str();
    }

    // Atomic write: write to .tmp, then rename
    std::string tmp_file = hash_file_ + ".tmp";
    std::string dir = utils::parent_dir(tmp_file);
    if (!dir.empty() && !fs::exists(dir)) {
        fs::create_directories(dir);
    }

    {
        std::ofstream f(tmp_file);
        if (!f.is_open()) {
            // Non-fatal: just skip saving
            return;
        }
        f << j.dump(2);
    }

    // Rename tmp to final (atomic on POSIX)
    std::error_code ec;
    fs::rename(tmp_file, hash_file_, ec);
    if (ec) {
        // Fallback: remove old and rename
        fs::remove(hash_file_, ec);
        fs::rename(tmp_file, hash_file_, ec);
    }
}

uint64_t HashStore::compute_hash(const std::string& data) {
    return XXH64(data.data(), data.size(), 0);
}

void HashStore::hash_file(const std::string& path) {
    if (!fs::exists(path)) return;

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return;

    std::ostringstream ss;
    ss << f.rdbuf();
    current_[path] = compute_hash(ss.str());
}

void HashStore::hash_string(const std::string& key, const std::string& contents) {
    current_[key] = compute_hash(contents);
}

bool HashStore::is_unchanged(const std::string& path) const {
    auto it = current_.find(path);
    if (it == current_.end()) return false; // new file = changed

    auto prev = previous_.find(path);
    if (prev == previous_.end()) return false; // no previous hash = changed

    return it->second == prev->second;
}

bool HashStore::is_unchanged_key(const std::string& key) const {
    auto it = current_.find(key);
    if (it == current_.end()) return false;

    auto prev = previous_.find(key);
    if (prev == previous_.end()) return false;

    return it->second == prev->second;
}

void HashStore::prune_deleted(const std::vector<std::string>& active_paths) {
    // Build a set of active paths for fast lookup
    std::unordered_map<std::string, bool> active;
    for (const auto& p : active_paths) {
        active[p] = true;
    }

    // Remove stored hashes for files no longer in the active set
    std::vector<std::string> to_remove;
    for (const auto& [key, _] : previous_) {
        if (active.find(key) == active.end()) {
            to_remove.push_back(key);
        }
    }

    for (const auto& key : to_remove) {
        previous_.erase(key);
    }
}

} // namespace cstatic
