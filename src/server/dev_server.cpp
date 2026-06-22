#include "server/dev_server.hpp"
#include "server/file_watcher.hpp"
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
    // Build the watch list once and hand it to FileWatcher; the watcher runs
    // for the lifetime of the server and calls rebuild_and_reload() on change.
    std::vector<std::string> watch_dirs;
    for (const auto& dir : {cfg_.source_dir, cfg_.template_dir, cfg_.static_dir, std::string(".")}) {
        if (fs::exists(dir)) {
            watch_dirs.push_back(dir);
        }
    }
    FileWatcher file_watcher(std::move(watch_dirs), [this]() { this->rebuild_and_reload(); });
    std::thread watcher([&file_watcher]() { file_watcher.start(); });

    // --- Graceful shutdown via signal ---
    // Static pointers so the C signal handler can reach the server + watcher.
    static DevServer* instance = this;
    static httplib::Server* svr_ptr = &svr;
    static FileWatcher* watcher_ptr = &file_watcher;
    std::signal(SIGINT, [](int) {
        if (instance) {
            instance->running_ = false;
            instance->sse_cv_.notify_all();
        }
        if (watcher_ptr) watcher_ptr->stop();
        if (svr_ptr) svr_ptr->stop();
    });

    // Start server (blocking)
    if (!svr.listen("0.0.0.0", port_)) {
        std::cerr << utils::error_label() << " cannot bind to port " << port_ << "\n";
        running_ = false;
        file_watcher.stop();
    }

    // Cleanup
    running_ = false;
    sse_cv_.notify_all();
    file_watcher.stop();
    if (watcher.joinable()) {
        watcher.join();
    }

    std::cout << "\n  Server stopped.\n";
}

void DevServer::stop() {
    running_ = false;
    sse_cv_.notify_all();
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
