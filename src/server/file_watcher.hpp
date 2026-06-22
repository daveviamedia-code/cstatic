#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace cstatic {

// Reusable cross-platform file watcher.
//
// Watches directories for modifications and invokes a callback (debounced)
// on any change. Used by both the dev server (live reload) and
// `cstatic build --watch` (rebuild on change without HTTP server).
//
// Platform implementations:
//   - macOS:   kqueue (EVFILT_VNODE, NOTE_WRITE)
//   - Linux:   inotify (IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_*)
//   - Windows: ReadDirectoryChangesW
//   - other:   std::filesystem polling fallback (1s resolution)
class FileWatcher {
public:
    using Callback = std::function<void()>;

    // Watch the given directories for changes. Calls cb (debounced) on any
    // filesystem event. Non-existent directories are silently skipped at
    // start() time. If no directories remain, start() returns immediately.
    FileWatcher(std::vector<std::string> dirs, Callback cb, int debounce_ms = 150);

    // Start watching. Blocks until stop() is called (or no dirs could be
    // opened). Safe to call from the main thread; the caller is responsible
    // for threading if concurrent execution is needed.
    void start();

    // Signal the watcher to stop. Safe to call from any thread, including
    // signal handlers. The start() call will return within ~1 second
    // (platform poll timeout).
    void stop();

private:
    // Platform-specific watch loop. Invokes cb_ on each debounced change.
    void watch_loop();

    std::vector<std::string> dirs_;
    Callback cb_;
    int debounce_ms_;
    std::atomic<bool> running_{false};
};

} // namespace cstatic
