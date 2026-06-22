#include "server/file_watcher.hpp"
#include "utils/terminal.hpp"

#include <filesystem>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <errno.h>
#include <unordered_map>

// Platform-specific file watching includes
#ifdef __APPLE__
  #include <sys/types.h>
  #include <sys/event.h>
  #include <sys/time.h>
  #include <unistd.h>
  #include <fcntl.h>
#elif defined(__linux__)
  #include <sys/inotify.h>
  #include <poll.h>
  #include <unistd.h>
  #include <limits.h>
#elif defined(_WIN32)
  #include <windows.h>
#endif

namespace cstatic {

namespace fs = std::filesystem;

FileWatcher::FileWatcher(std::vector<std::string> dirs, Callback cb, int debounce_ms)
    : dirs_(std::move(dirs)), cb_(std::move(cb)), debounce_ms_(debounce_ms) {}

void FileWatcher::start() {
    running_ = true;
    watch_loop();
    running_ = false;
}

void FileWatcher::stop() {
    running_ = false;
}

void FileWatcher::watch_loop() {
    // Filter to existing directories. If none remain, there is nothing to
    // watch — start() should return immediately rather than block forever.
    std::vector<std::string> watch_dirs;
    for (const auto& dir : dirs_) {
        if (fs::exists(dir)) {
            watch_dirs.push_back(dir);
        }
    }

    if (watch_dirs.empty()) {
        return;
    }

#ifdef __APPLE__
    // --- kqueue (macOS) ---
    int kq = kqueue();
    if (kq == -1) {
        std::cerr << utils::error_label() << " kqueue creation failed: "
                  << strerror(errno) << "\n";
        return;
    }

    std::vector<int> watch_fds;
    for (const auto& dir : watch_dirs) {
        int fd = open(dir.c_str(), O_EVTONLY);
        if (fd == -1) continue;
        watch_fds.push_back(fd);

        struct kevent change;
        EV_SET(&change, fd, EVFILT_VNODE,
               EV_ADD | EV_ENABLE | EV_CLEAR,
               NOTE_WRITE, 0, nullptr);
        kevent(kq, &change, 1, nullptr, 0, nullptr);
    }

    while (running_) {
        struct timespec timeout;
        timeout.tv_sec = 1;
        timeout.tv_nsec = 0;

        struct kevent event;
        int nev = kevent(kq, nullptr, 0, &event, 1, &timeout);

        if (!running_) break;
        if (nev <= 0) continue;

        // Debounce — coalesce bursts of writes into a single callback.
        std::this_thread::sleep_for(std::chrono::milliseconds(debounce_ms_));

        // Drain remaining queued events so they don't retrigger the callback.
        while (true) {
            struct timespec ts = {0, 0};
            struct kevent ev;
            if (kevent(kq, nullptr, 0, &ev, 1, &ts) <= 0) break;
        }

        if (cb_) cb_();
    }

    for (int fd : watch_fds) {
        close(fd);
    }
    close(kq);

#elif defined(__linux__)
    // --- inotify (Linux) ---
    int inofd = inotify_init();
    if (inofd == -1) {
        std::cerr << utils::error_label() << " inotify_init failed: "
                  << strerror(errno) << "\n";
        return;
    }

    std::vector<int> watch_fds;
    for (const auto& dir : watch_dirs) {
        int wd = inotify_add_watch(inofd, dir.c_str(),
                                   IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
        if (wd == -1) continue;
        watch_fds.push_back(wd);
    }

    alignas(struct inotify_event) char buf[4096];

    while (running_) {
        struct pollfd pfd;
        pfd.fd = inofd;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 1000); // 1s timeout so we can re-check running_

        if (!running_) break;
        if (ret <= 0) continue;

        // Debounce
        std::this_thread::sleep_for(std::chrono::milliseconds(debounce_ms_));

        // Drain events
        while (true) {
            ssize_t len = read(inofd, buf, sizeof(buf));
            if (len <= 0) break;
        }

        if (cb_) cb_();
    }

    for (int wd : watch_fds) {
        inotify_rm_watch(inofd, wd);
    }
    close(inofd);

#elif defined(_WIN32)
    // --- ReadDirectoryChangesW (Windows) ---
    std::vector<HANDLE> handles;
    std::vector<char> buffers(watch_dirs.size() * 4096);
    std::vector<OVERLAPPED> ovls(watch_dirs.size());

    for (size_t i = 0; i < watch_dirs.size(); i++) {
        HANDLE hDir = CreateFileA(
            watch_dirs[i].c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr
        );
        if (hDir == INVALID_HANDLE_VALUE) continue;

        ZeroMemory(&ovls[i], sizeof(OVERLAPPED));
        ReadDirectoryChangesW(
            hDir, &buffers[i * 4096], 4096, TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
            FILE_NOTIFY_CHANGE_LAST_WRITE,
            nullptr, &ovls[i], nullptr
        );
        handles.push_back(hDir);
    }

    while (running_ && !handles.empty()) {
        DWORD wait = WaitForMultipleObjects(
            static_cast<DWORD>(handles.size()), handles.data(),
            FALSE, 1000
        );

        if (!running_) break;
        if (wait == WAIT_TIMEOUT) continue;

        // Debounce
        Sleep(debounce_ms_);

        // Re-arm all watches
        for (size_t i = 0; i < handles.size(); i++) {
            ZeroMemory(&ovls[i], sizeof(OVERLAPPED));
            ReadDirectoryChangesW(
                handles[i], &buffers[i * 4096], 4096, TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                FILE_NOTIFY_CHANGE_LAST_WRITE,
                nullptr, &ovls[i], nullptr
            );
        }

        if (cb_) cb_();
    }

    for (HANDLE h : handles) {
        CloseHandle(h);
    }

#else
    // --- Polling fallback (any other platform) ---
    std::unordered_map<std::string, fs::file_time_type> snapshots;
    for (const auto& dir : watch_dirs) {
        for (const auto& entry : fs::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::error_code ec;
                auto lwt = entry.last_write_time(ec);
                if (!ec) {
                    snapshots[entry.path().string()] = lwt;
                }
            }
        }
    }

    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!running_) break;

        bool changed = false;
        for (auto& [path, lwt] : snapshots) {
            std::error_code ec;
            auto current = fs::last_write_time(path, ec);
            if (!ec && current != lwt) {
                lwt = current;
                changed = true;
            }
        }

        if (changed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(debounce_ms_));
            if (cb_) cb_();
        }
    }
#endif
}

} // namespace cstatic
