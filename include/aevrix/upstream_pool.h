// =============================================================================
// Aevrix - Upstream Connection Pool
// =============================================================================
// Phase 22: keeps a bounded set of idle keep-alive connections to upstream
// servers so that proxied requests do not pay a TCP handshake each time.
//
// Design notes:
//
// - The pool owns its descriptors through UniqueFd, so an error path can never
//   leak a socket: every exit point either transfers ownership to the caller or
//   closes the descriptor.
// - Sockets are created non-blocking and connect() is allowed to return
//   EINPROGRESS. The caller waits for the descriptor to become writable in the
//   event loop and only then checks SO_ERROR. A pool must never block the event
//   loop: a slow or dead upstream is exactly what the timeout machinery exists
//   for.
// - Pools are keyed by authority ("host:port"), so a connection is only ever
//   reused for the upstream it was opened for.
// - Idle connections are evicted both by age (sweep_idle) and by capacity
//   (max_idle_per_target), which keeps descriptor usage bounded.
//
// Resolution is deliberately *not* done here: the caller resolves the target
// once at startup and passes a ready sockaddr, so no DNS lookup (which blocks)
// can happen inside an event callback.
//
// Platform: POSIX sockets. The proxy runtime is Linux-first, matching the
// event-driven main loop; other platforms fall back to the non-proxy path.
// =============================================================================

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef _WIN32
#include <sys/socket.h>
#endif

#include "aevrix/proxy_target.h"
#include "aevrix/unique_fd.h"

namespace aevrix {

/**
 * @brief Bounded pool of idle upstream connections
 */
class UpstreamPool {
public:
    /**
     * @brief Outcome of an acquire() call
     */
    struct AcquireResult {
        bool ok = false;        // A usable descriptor was produced
        bool reused = false;    // The descriptor came from the idle set
        UniqueFd fd;            // Owned descriptor (invalid when !ok)
        std::string error;      // Human-readable failure reason

        AcquireResult() = default;

        // Move-only: the result owns a descriptor.
        AcquireResult(AcquireResult&&) noexcept = default;
        AcquireResult& operator=(AcquireResult&&) noexcept = default;
        AcquireResult(const AcquireResult&) = delete;
        AcquireResult& operator=(const AcquireResult&) = delete;
    };

    UpstreamPool() = default;
    ~UpstreamPool();

    // The pool owns descriptors and must not be duplicated.
    UpstreamPool(const UpstreamPool&) = delete;
    UpstreamPool& operator=(const UpstreamPool&) = delete;

    // =========================================================================
    // Configuration
    // =========================================================================

    void set_max_idle_per_target(size_t value) { max_idle_per_target_ = value; }
    size_t max_idle_per_target() const { return max_idle_per_target_; }

    void set_idle_timeout_ms(uint64_t value) { idle_timeout_ms_ = value; }
    uint64_t idle_timeout_ms() const { return idle_timeout_ms_; }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Obtain a connection to an upstream
     *
     * Returns an idle connection when one is available, otherwise creates a
     * non-blocking socket and starts connecting. A descriptor returned with
     * "created" semantics is not yet connected: the caller must wait for
     * writability and verify the socket error.
     *
     * @param target The upstream identity (pool key)
     * @param address Pre-resolved address of the upstream
     * @param address_length Length of @p address
     * @return AcquireResult The descriptor, or an error description
     */
    AcquireResult acquire(const ProxyTarget& target,
                          const struct sockaddr* address,
                          socklen_t address_length);

    /**
     * @brief Return a connection for reuse
     *
     * The descriptor is pooled when the pool has room; otherwise it is closed.
     *
     * @param target The upstream the connection belongs to
     * @param fd The descriptor (ownership transfers to the pool)
     */
    void release(const ProxyTarget& target, UniqueFd fd);

    /**
     * @brief Close idle connections whose idle timeout elapsed
     *
     * @return size_t Number of connections closed
     */
    size_t sweep_idle();

    /**
     * @brief Close every idle connection (graceful shutdown)
     */
    void close_all();

    // =========================================================================
    // Introspection (used by tests and logs)
    // =========================================================================

    size_t idle_count() const;
    size_t target_count() const { return idle_.size(); }

private:
    /**
     * @brief An idle connection and the time it became idle
     */
    struct IdleConnection {
        UniqueFd fd;
        std::chrono::steady_clock::time_point since;

        IdleConnection(UniqueFd descriptor, std::chrono::steady_clock::time_point at)
            : fd(std::move(descriptor)), since(at) {}

        IdleConnection(IdleConnection&&) noexcept = default;
        IdleConnection& operator=(IdleConnection&&) noexcept = default;
        IdleConnection(const IdleConnection&) = delete;
        IdleConnection& operator=(const IdleConnection&) = delete;
    };

    /**
     * @brief Create a non-blocking socket and start connecting
     */
    AcquireResult create(const struct sockaddr* address, socklen_t address_length);

    std::unordered_map<std::string, std::vector<IdleConnection>> idle_;
    size_t max_idle_per_target_ = 4;
    uint64_t idle_timeout_ms_ = 60000;
};

} // namespace aevrix
