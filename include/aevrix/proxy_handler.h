// =============================================================================
// Aevrix - Proxy Handler
// =============================================================================
// Phase 22: the reverse-proxy request lifecycle.
//
//   client → Aevrix → upstream
//
// The handler owns every in-flight proxy request. Each request is a small state
// machine driven directly by the event loop:
//
//   Connecting ──writable──▶ Writing ──flushed──▶ Reading ──complete──▶ done
//        │                      │                    │
//        └──────────── failure / timeout ────────────┘
//
// Why the event loop and not the worker pool?
// Worker threads exist for blocking *filesystem* work. Upstream I/O is network
// I/O, so it stays non-blocking on the event loop thread: no worker thread ever
// touches a socket, and no proxy request can stall other clients.
//
// Relationship with the client connection:
// The handler never writes to the client socket. It reports a ProxyOutcome
// through a completion callback; main.cpp turns that into an HttpResponse and
// reuses the existing "set output buffer + EPOLLOUT" path. That keeps the
// client-side write state machine, keep-alive handling and timeout sweep
// exactly as they are for static files.
//
// Timeout model (two dimensions):
// - Client-side: main.cpp marks the connection busy (set_worker_active) while
//   the upstream works, so the ordinary header/write deadlines do not fire.
// - Upstream-side: connect and read/parse have their own deadlines, enforced by
//   sweep_timeouts() from the server's periodic maintenance loop. A stalled
//   upstream therefore yields 504 instead of holding the client forever.
//
// Lifetime: the handler must be destroyed *before* the event loop it was given,
// because the destructor removes the upstream descriptors it registered.
// =============================================================================

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#ifndef _WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "aevrix/event_loop.h"
#include "aevrix/http_header.h"
#include "aevrix/http_request.h"
#include "aevrix/proxy_response_parser.h"
#include "aevrix/proxy_target.h"
#include "aevrix/server_config.h"
#include "aevrix/unique_fd.h"
#include "aevrix/upstream_pool.h"

namespace aevrix {

/**
 * @brief How a proxied exchange ended
 */
enum class ProxyOutcomeStatus {
    Success,          // The upstream response was received in full
    BadGateway,       // Connect failure, protocol violation, truncated response
    GatewayTimeout,   // Upstream connect or read deadline exceeded
    UpstreamTooLarge, // The upstream response exceeded the configured limit
    NotConfigured,    // proxy_pass could not be parsed or resolved
    Unavailable       // Resources exhausted (socket creation, pool)
};

/**
 * @brief The result of a proxied exchange, delivered to the completion callback
 */
struct ProxyOutcome {
    ProxyOutcomeStatus status = ProxyOutcomeStatus::BadGateway;
    int upstream_status = 0;          // Valid when status == Success
    http::HttpHeaders headers;        // Upstream response headers (Success)
    std::string body;                 // Upstream response body (Success)
    std::string error_message;        // Diagnostic text for failures
    bool head_request = false;        // The client request was HEAD
};

/**
 * @brief Called exactly once per accepted proxy request
 *
 * @param client_connection_id The client connection that triggered the request
 * @param outcome The upstream result
 */
using ProxyCompletion = std::function<void(uint64_t client_connection_id, ProxyOutcome outcome)>;

/**
 * @brief Reverse-proxy request handler
 */
class ProxyHandler {
public:
    /**
     * @brief Construct the handler and resolve the configured upstream
     *
     * Resolution happens here, at startup, so that no blocking DNS lookup can
     * occur inside an event callback.
     *
     * @param config Server configuration (proxy settings are read here)
     * @param event_loop The event loop used for upstream descriptors
     * @param on_complete Callback invoked once per request with the outcome
     */
    ProxyHandler(const ServerConfig& config, EventLoop& event_loop, ProxyCompletion on_complete);
    ~ProxyHandler();

    ProxyHandler(const ProxyHandler&) = delete;
    ProxyHandler& operator=(const ProxyHandler&) = delete;

    // =========================================================================
    // Routing
    // =========================================================================

    /**
     * @brief Whether the proxy is enabled and the request path is proxied
     *
     * @param request The client request
     * @return true if the request must be forwarded upstream
     */
    bool handles(const http::HttpRequest& request) const;

    /**
     * @brief Start proxying a request
     *
     * Always takes ownership of the response: the completion callback is
     * invoked exactly once, possibly synchronously when the request cannot be
     * started at all.
     *
     * @param client_connection_id The client connection identifier
     * @param client_fd The client descriptor (used for the peer address only)
     * @param request The parsed client request
     * @return true if the callback will be invoked (always true today)
     */
    bool start(uint64_t client_connection_id, int client_fd, const http::HttpRequest& request);

    // =========================================================================
    // Maintenance
    // =========================================================================

    /**
     * @brief Enforce upstream deadlines and evict idle pooled connections
     *
     * Called from the server's periodic maintenance loop (the same place as
     * ConnectionManager::sweep_timeouts).
     */
    void sweep_timeouts();

    /**
     * @brief Abandon every in-flight request and close pooled connections
     */
    void shutdown();

    // =========================================================================
    // Introspection (logs, tests, metrics)
    // =========================================================================

    bool is_configured() const { return configured_; }
    const std::string& configuration_error() const { return configuration_error_; }
    size_t active_request_count() const { return pending_.size(); }
    size_t pooled_idle_connections() const { return pool_.idle_count(); }
    uint64_t completed_requests() const { return completed_requests_; }
    uint64_t failed_requests() const { return failed_requests_; }
    uint64_t connections_created() const { return connections_created_; }
    uint64_t connections_reused() const { return connections_reused_; }

private:
    /**
     * @brief Phase of an in-flight exchange
     */
    enum class Phase {
        Connecting,  // Waiting for the non-blocking connect to complete
        Writing,     // Flushing the request to the upstream
        Reading      // Waiting for the upstream response
    };

    /**
     * @brief Per-request state (owned, keyed by upstream descriptor)
     */
    struct PendingRequest {
        uint64_t client_connection_id = 0;
        int client_fd = -1;

        // Owns the upstream descriptor: closing, pooling or abandoning the
        // request all release the socket deterministically.
        UniqueFd connection;
        int upstream_fd = -1;

        Phase phase = Phase::Connecting;
        ProxyTarget target;
        ProxyResponseParser parser;
        std::string request_bytes;
        size_t bytes_written = 0;
        std::chrono::steady_clock::time_point deadline;
        bool head_request = false;

        // Descriptor identity. EventLoop copies callbacks before dispatching
        // them, so a stale event can still arrive for a descriptor number that
        // has since been closed and re-used by a pooled connection. The
        // generation makes such an event a no-op.
        uint64_t generation = 0;
    };

    // Event loop callbacks
    void on_upstream_event(int fd, uint64_t generation, EventType event);
    void on_writable(PendingRequest& request);
    void on_readable(PendingRequest& request);

    // Phase transitions
    bool finish_connect(PendingRequest& request);
    bool flush_request(PendingRequest& request);
    void read_available(PendingRequest& request);

    // Completion
    void complete(int fd, ProxyOutcome outcome);
    void fail(int fd, ProxyOutcomeStatus status, const std::string& message);
    void release_upstream(int fd, PendingRequest& request);

    std::chrono::steady_clock::time_point deadline_after(uint64_t timeout_ms) const;

    // =========================================================================
    // State
    // =========================================================================

    const ServerConfig& config_;
    EventLoop& event_loop_;
    ProxyCompletion on_complete_;
    UpstreamPool pool_;
    std::unordered_map<int, PendingRequest> pending_;

    ProxyTarget target_;
    bool configured_ = false;         // Target parsed and resolved
    std::string configuration_error_;
    struct sockaddr_storage upstream_address_ {};
    socklen_t upstream_address_length_ = 0;

    uint64_t completed_requests_ = 0;
    uint64_t failed_requests_ = 0;
    uint64_t connections_created_ = 0;
    uint64_t connections_reused_ = 0;
    uint64_t next_generation_ = 1;    // Monotonic descriptor identity counter
};

} // namespace aevrix
