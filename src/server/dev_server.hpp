#pragma once

#include "config/config.hpp"
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace cstatic {

// Development server with live reload.
// Serves built files from output/, watches source directories for changes,
// triggers incremental rebuilds, and pushes reload notifications to browsers
// via Server-Sent Events (SSE).
class DevServer {
public:
    explicit DevServer(const Config& cfg, int port = 3000, bool include_drafts = false);
    ~DevServer();

    // Start the server (blocking). Returns on stop() or fatal error.
    void start();

    // Signal the server to stop (safe to call from any thread).
    void stop();

private:
    // Trigger an incremental rebuild and notify connected browsers.
    void rebuild_and_reload();

    // --- SSE live reload ---
    // Notify all connected SSE clients to reload.
    void broadcast_reload();

    Config cfg_;
    int port_;
    bool include_drafts_;
    std::atomic<bool> running_{false};

    // SSE client count (for display purposes)
    std::mutex sse_mutex_;
    int sse_client_count_ = 0;

    // Condition variable to wake SSE clients
    std::condition_variable sse_cv_;
    std::mutex sse_notify_mutex_;
    bool reload_pending_ = false;
};

} // namespace cstatic
