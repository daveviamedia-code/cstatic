#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "server/file_watcher.hpp"

namespace fs = std::filesystem;
using cstatic::FileWatcher;

namespace {

// Per-test temp directory. FileWatcher needs a real filesystem to trigger
// kernel notifications (kqueue/inotify), so we can't fake this in-memory.
struct WatchFixture {
    std::string root_dir;
    std::string saved_cwd;

    WatchFixture() {
        saved_cwd = fs::current_path().string();
        root_dir = (fs::temp_directory_path() /
                    ("cstatic_fw_" + std::to_string(std::rand()))).string();
        fs::create_directories(root_dir);
        fs::current_path(root_dir);
    }

    ~WatchFixture() {
        fs::current_path(saved_cwd);
        std::error_code ec;
        fs::remove_all(root_dir, ec);
    }

    // Write (or overwrite) a file under the watched root.
    void write(const std::string& rel, const std::string& content) {
        fs::path p = fs::path(root_dir) / rel;
        fs::create_directories(p.parent_path());
        std::ofstream f(p);
        f << content;
        f.close();
        REQUIRE(f.good());
    }

    // Poll a predicate until it returns true or timeout (seconds) elapses.
    // File watchers are inherently async, so tests must poll rather than
    // assume immediate delivery.
    static bool wait_for(std::atomic<bool>& flag, int timeout_ms = 3000) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (flag.load()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        return flag.load();
    }
};

} // namespace

TEST_CASE("FileWatcher invokes callback on file modification", "[file_watcher]") {
    WatchFixture fx;

    std::atomic<int> calls{0};
    std::atomic<bool> fired{false};
    FileWatcher watcher({fx.root_dir}, [&]() {
        calls.fetch_add(1);
        fired.store(true);
    });

    // Run watcher on its own thread — start() blocks.
    std::thread t([&]() { watcher.start(); });

    // Give the kernel a moment to register the watch handle.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Creating a new file in the watched directory reliably triggers the
    // directory-level event on all platforms (kqueue NOTE_WRITE on macOS,
    // IN_CREATE on Linux, FILE_NOTIFY_CHANGE_FILE_NAME on Windows).
    // Editing an existing file's contents is less reliable on macOS kqueue.
    fx.write("new-file.txt", "hello\n");

    // Wait for callback to fire.
    REQUIRE(WatchFixture::wait_for(fired));

    watcher.stop();
    t.join();

    REQUIRE(calls.load() >= 1);
}

TEST_CASE("FileWatcher stop() exits the start() loop", "[file_watcher]") {
    WatchFixture fx;
    fx.write("a.txt", "x\n");

    std::atomic<int> calls{0};
    FileWatcher watcher({fx.root_dir}, [&]() { calls.fetch_add(1); });

    // start() should block indefinitely on this thread until stop() is called
    // from below.
    std::thread t([&]() { watcher.start(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    // No modification → no callback expected yet.
    REQUIRE(calls.load() == 0);

    watcher.stop();
    // The watcher thread should exit within ~1s (platform poll timeout).
    // If stop() failed to unblock start(), this join hangs and CTest times
    // the test out — which is the failure signal we want.
    t.join();
}

TEST_CASE("FileWatcher returns immediately when no dirs exist", "[file_watcher]") {
    std::atomic<int> calls{0};
    // A path that definitely doesn't exist.
    FileWatcher watcher({"/definitely/not/a/real/path/cstatic_test_42"},
                        [&]() { calls.fetch_add(1); });

    auto start = std::chrono::steady_clock::now();
    watcher.start();  // should not block
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start);

    REQUIRE(elapsed.count() < 500);  // conservatively under half a second
    REQUIRE(calls.load() == 0);
}

TEST_CASE("FileWatcher construction does not start watching", "[file_watcher]") {
    WatchFixture fx;
    fx.write("note.txt", "initial\n");

    std::atomic<int> calls{0};
    {
        FileWatcher watcher({fx.root_dir}, [&]() { calls.fetch_add(1); });
        // Constructed but start() not called — no callbacks should fire.
        // Also create a new file while the watcher exists but isn't running.
        fx.write("other.txt", "y\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    } // watcher destroyed

    REQUIRE(calls.load() == 0);
}
