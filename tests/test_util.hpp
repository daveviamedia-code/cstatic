#pragma once

// Shared test helpers for the C-Static test suite.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

namespace cstatic_test {

// Returns the path of a unique temp directory (NOT yet created) for use by
// test fixtures. The name combines a monotonic nanosecond timestamp with a
// process-local atomic counter so that:
//   - parallel ctest processes get distinct names from the timestamp, and
//   - multiple constructions within a single process get distinct names
//     from the counter.
//
// This replaces the prior `std::rand()`-based names. `std::rand()` was never
// seeded (default seed = 1), so every process's first call returned the same
// value and parallel integration tests collided on the same `/tmp` dir —
// the root cause of intermittent, different-test-each-run CI failures.
inline std::string unique_temp_dir(const std::string& prefix) {
    static std::atomic<unsigned long long> counter{0};
    const auto ns = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto c = counter.fetch_add(1, std::memory_order_relaxed);
    const auto name = prefix + std::to_string(ns) + "_" + std::to_string(c);
    return (std::filesystem::temp_directory_path() / name).string();
}

} // namespace cstatic_test
