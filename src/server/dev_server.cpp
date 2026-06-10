#include "server/dev_server.hpp"
#include "pipeline/builder.hpp"
#include "config/config.hpp"
#include "utils/path.hpp"
#include "utils/terminal.hpp"

#include <httplib.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <errno.h>

// kqueue for macOS file watching
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

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

// Read a file into a string. Returns empty on failure.
static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
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

DevServer::DevServer(const Config& cfg, int port)
    : cfg_(cfg), port_(port) {}

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

        std::string content = read_file(file_path);
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
    int kq = kqueue();
    if (kq == -1) {
        std::cerr << utils::error_label() << " kqueue creation failed: "
                  << strerror(errno) << "\n";
        return;
    }

    // Directories to watch
    std::vector<std::string> watch_dirs = {
        cfg_.source_dir,
        cfg_.template_dir,
        cfg_.static_dir,
        ".",  // for config.toml
    };

    // Register watches
    std::vector<int> watch_fds;
    for (const auto& dir : watch_dirs) {
        if (!fs::exists(dir)) continue;

        int fd = open(dir.c_str(), O_EVTONLY);
        if (fd == -1) continue;
        watch_fds.push_back(fd);

        struct kevent change;
        EV_SET(&change, fd, EVFILT_VNODE,
               EV_ADD | EV_ENABLE | EV_CLEAR,
               NOTE_WRITE, 0, nullptr);
        kevent(kq, &change, 1, nullptr, 0, nullptr);
    }

    // Event loop
    while (running_) {
        struct timespec timeout;
        timeout.tv_sec = 1;
        timeout.tv_nsec = 0;

        struct kevent event;
        int nev = kevent(kq, nullptr, 0, &event, 1, &timeout);

        if (!running_) break;
        if (nev <= 0) continue;

        // Debounce: wait for rapid events to settle
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        // Drain remaining queued events
        while (true) {
            struct timespec ts = {0, 0};
            struct kevent ev;
            if (kevent(kq, nullptr, 0, &ev, 1, &ts) <= 0) break;
        }

        // Trigger rebuild
        rebuild_and_reload();
    }

    // Cleanup
    for (int fd : watch_fds) {
        close(fd);
    }
    close(kq);
}

void DevServer::rebuild_and_reload() {
    try {
        Config cfg = load_config("config.toml");
        auto result = build_site(cfg, false);

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
