#include "server/dev_server.hpp"
#include "pipeline/builder.hpp"
#include "config/config.hpp"
#include "utils/path.hpp"
#include "utils/terminal.hpp"
#include "utils/file_io.hpp"

#include <httplib.h>

#include <filesystem>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <errno.h>

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

// --- MIME type helper ---

static const char* mime_type(const std::string& path) {
    static const std::pair<std::string, const char*> types[] = {
        {".html", "text/html"},
        {".css",  "text/css"},
        {".js",   "application/javascript"},
        {".json", "application/json"},
        {".png",  "image/png"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif",  "image/gif"},
        {".svg",  "image/svg+xml"},
        {".ico",  "image/x-icon"},
        {".webp", "image/webp"},
        {".woff", "font/woff"},
        {".woff2","font/woff2"},
        {".ttf",  "font/ttf"},
        {".xml",  "application/xml"},
        {".txt",  "text/plain"},
        {".pdf",  "application/pdf"},
        {".mp4",  "video/mp4"},
        {".webm", "video/webm"},
        {".mp3",  "audio/mpeg"},
        {".webmanifest", "application/manifest+json"},
    };

    std::string ext;
    auto dot = path.rfind('.');
    if (dot != std::string::npos) {
        ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    }

    for (const auto& [e, t] : types) {
        if (ext == e) return t;
    }
    return "application/octet-stream";
}

// --- Live reload script (injected into HTML pages via SSE) ---

static const char* RELOAD_SCRIPT = R"(
<script>
(function() {
    var es = new EventSource('/__cstatic_reload');
    es.onmessage = function(e) {
        if (e.data === 'reload') {
            location.reload();
        }
    };
    es.onerror = function() {
        setTimeout(function() { location.reload(); }, 2000);
    };
})();
</script>
)";

// --- DevServer implementation ---

DevServer::DevServer(const Config& cfg, int port, bool include_drafts)
    : cfg_(cfg), port_(port), include_drafts_(include_drafts) {}

DevServer::~DevServer() {
    stop();
}

void DevServer::start() {
    running_ = true;

    httplib::Server svr;

    std::string output_dir = cfg_.output_dir;

    // --- SSE endpoint for live reload (register BEFORE catch-all) ---
    svr.Get("/__cstatic_reload", [this](const httplib::Request&, httplib::Response& res) {
        res.set_chunked_content_provider(
            "text/event-stream",
            [this](size_t /*offset*/, httplib::DataSink& sink) -> bool {
                {
                    std::lock_guard<std::mutex> lock(sse_mutex_);
                    sse_client_count_++;
                }

                // Send initial comment to establish connection
                const char* hello = ": connected\n\n";
                sink.write(hello, strlen(hello));

                while (running_) {
                    // Wait for reload signal or timeout
                    std::unique_lock<std::mutex> lock(sse_notify_mutex_);
                    bool signaled = sse_cv_.wait_for(
                        lock,
                        std::chrono::seconds(30),
                        [this] { return reload_pending_ || !running_; }
                    );

                    if (!running_) break;

                    if (signaled && reload_pending_) {
                        reload_pending_ = false;
                        const char* msg = "data: reload\n\n";
                        sink.write(msg, strlen(msg));
                    } else {
                        // Heartbeat to keep connection alive
                        const char* heartbeat = ": ping\n\n";
                        sink.write(heartbeat, strlen(heartbeat));
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(sse_mutex_);
                    sse_client_count_--;
                }
                return true;
            }
        );
    });

    // --- Manual file serving from output/ (with script injection for HTML) ---
    svr.Get(".*", [output_dir](const httplib::Request& req, httplib::Response& res) {
        // Resolve path — default to index.html for directories
        std::string rel = req.path;
        if (rel.empty() || rel == "/") rel = "/index.html";

        // Security: prevent path traversal
        if (rel.find("..") != std::string::npos) {
            res.status = 400;
            res.set_content("Bad request", "text/plain");
            return;
        }

        // Strip leading slash
        if (!rel.empty() && rel.front() == '/') {
            rel = rel.substr(1);
        }

        std::string file_path = utils::path_join(output_dir, rel);

        // If path is a directory, try index.html inside it
        if (fs::is_directory(file_path)) {
            file_path = utils::path_join(file_path, "index.html");
        }

        if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
            res.status = 404;
            res.set_content(
                "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                "<title>404 Not Found</title></head>"
                "<body><h1>404 Not Found</h1>"
                "<p>The page you requested does not exist.</p>"
                "</body></html>",
                "text/html"
            );
            return;
        }

        std::string content = utils::read_file_or_empty(file_path);
        const char* mime = mime_type(file_path);

        // Inject live reload script into HTML pages
        if (std::string(mime).find("text/html") != std::string::npos) {
            auto head_pos = content.rfind("</head>");
            if (head_pos != std::string::npos) {
                content.insert(head_pos, RELOAD_SCRIPT);
            } else {
                auto body_pos = content.rfind("</body>");
                if (body_pos != std::string::npos) {
                    content.insert(body_pos, RELOAD_SCRIPT);
                } else {
                    content += RELOAD_SCRIPT;
                }
            }
        }

        res.set_content(content, mime);
    });

    // --- Print startup message ---
    std::string title = utils::colorize(utils::color::bold,
        utils::colorize(utils::color::green, "C-Static dev server"));
    std::string url = utils::colorize(utils::color::cyan,
        "http://localhost:" + std::to_string(port_));
    std::cout << "\n  " << title << "\n\n"
              << "  " << url << "\n\n"
              << "  Watching for changes... (Ctrl+C to stop)\n\n" << std::flush;

    // --- Start file watcher in a separate thread ---
    std::thread watcher(&DevServer::watch_loop, this);

    // --- Graceful shutdown via signal ---
    // Use a static pointer so the C signal handler can access the server.
    static DevServer* instance = this;
    std::signal(SIGINT, [](int) {
        if (instance) {
            instance->running_ = false;
            instance->sse_cv_.notify_all();
        }
    });

    // Start server (blocking)
    if (!svr.listen("0.0.0.0", port_)) {
        std::cerr << utils::error_label() << " cannot bind to port " << port_ << "\n";
        running_ = false;
    }

    // Cleanup
    running_ = false;
    sse_cv_.notify_all();
    if (watcher.joinable()) {
        watcher.join();
    }

    std::cout << "\n  Server stopped.\n";
}

void DevServer::stop() {
    running_ = false;
    sse_cv_.notify_all();
}

// --- File watching via kqueue (macOS) ---

void DevServer::watch_loop() {
    // Directories to watch (shared across all platforms)
    std::vector<std::string> watch_dirs;
    for (const auto& dir : {cfg_.source_dir, cfg_.template_dir, cfg_.static_dir, std::string(".")}) {
        if (fs::exists(dir)) {
            watch_dirs.push_back(dir);
        }
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

        // Debounce
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        // Drain remaining queued events
        while (true) {
            struct timespec ts = {0, 0};
            struct kevent ev;
            if (kevent(kq, nullptr, 0, &ev, 1, &ts) <= 0) break;
        }

        rebuild_and_reload();
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
        int ret = poll(&pfd, 1, 1000); // 1s timeout

        if (!running_) break;
        if (ret <= 0) continue;

        // Debounce
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        // Drain events
        while (true) {
            ssize_t len = read(inofd, buf, sizeof(buf));
            if (len <= 0) break;
        }

        rebuild_and_reload();
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
        Sleep(150);

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

        rebuild_and_reload();
    }

    for (HANDLE h : handles) {
        CloseHandle(h);
    }

#else
    // --- Polling fallback ---
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
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            rebuild_and_reload();
        }
    }
#endif
}

void DevServer::rebuild_and_reload() {
    try {
        Config cfg = load_config("config.toml");
        // full_rebuild=false respects cfg.incremental_enabled — so dev mode
        // uses incremental builds when the user has them enabled (default).
        auto result = build_site(cfg, false, include_drafts_);

        // Fallback safety: if an incremental build reports zero work done
        // with no errors, the cache may be stale relative to the output dir
        // (e.g. output wiped manually while hashes.json survived). Retry once
        // as a forced full rebuild so the dev server self-heals instead of
        // silently serving a broken/empty site.
        const bool suspicious_zero = cfg.incremental_enabled &&
            result.pages_built == 0 &&
            result.pages_cached == 0 &&
            result.errors.empty();
        if (suspicious_zero) {
            std::cerr << "  " << utils::warning_label()
                      << " incremental rebuild reported no work; retrying as full rebuild\n";
            result = build_site(cfg, true, include_drafts_);
        }

        // Print rebuild summary
        std::string msg;
        if (result.pages_cached > 0) {
            msg = "[" + std::to_string(static_cast<int>(result.elapsed_ms)) + "ms] " +
                  std::to_string(result.pages_built) + " rebuilt, " +
                  std::to_string(result.pages_cached) + " cached";
        } else {
            msg = "[" + std::to_string(static_cast<int>(result.elapsed_ms)) + "ms] " +
                  std::to_string(result.pages_built) + " page(s) built";
        }
        std::cout << "  " << utils::colorize(utils::color::dim, msg) << "\n" << std::flush;

        // Notify browsers to reload
        broadcast_reload();

    } catch (const std::exception& e) {
        std::cerr << "  " << utils::error_label() << " rebuild failed: "
                  << e.what() << "\n";
    }
}

void DevServer::broadcast_reload() {
    {
        std::lock_guard<std::mutex> lock(sse_notify_mutex_);
        reload_pending_ = true;
    }
    sse_cv_.notify_all();
}

} // namespace cstatic
