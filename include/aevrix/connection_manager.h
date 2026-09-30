// =============================================================================
// Aevrix - Connection Manager
// =============================================================================
// This file implements the ConnectionManager class for managing active client
// connections in the event-driven HTTP server. The ConnectionManager is responsible
// for connection lifecycle, timeout enforcement, and resource limit enforcement.
//
// Stage 2 - Event-Driven Refactor:
// - Owns and tracks all active client connections
// - Registers new connections and assigns unique IDs
// - Removes closed, timed out, or errored connections
// - Enforces maximum connection limits
// - Sweeps inactive connections based on timeout policies
// - Provides thread-safe connection lookup and management
//
// Architecture:
// EventLoop → ConnectionManager → Connection → HTTP Parser/Response
//
// Responsibilities:
// - Connection registration and tracking
// - Connection removal and cleanup
// - Timeout enforcement (header, body, write, keep-alive)
// - Resource limit enforcement (max_connections)
// - Connection lifecycle management
// =============================================================================

#pragma once

#include <unordered_map>
#include <memory>
#include <cstdint>
#include <mutex>
#include <vector>
#include "aevrix/connection.h"
#include "aevrix/server_config.h"

namespace aevrix {

/**
 * @brief Connection Manager for managing active client connections
 * 
 * The ConnectionManager owns and tracks all active client connections in the
 * event-driven server. It provides thread-safe operations for connection
 * registration, lookup, removal, and timeout enforcement.
 * 
 * Key Features:
 * - Thread-safe connection management (uses mutex)
 * - Unique connection ID assignment
 * - File descriptor-based lookup
 * - Automatic connection cleanup on removal
 * - Timeout sweeping for inactive connections
 * - Resource limit enforcement (max_connections)
 * 
 * Thread Safety:
 * All public methods are thread-safe and can be called from any thread.
 * Internally uses a mutex to protect the connection map.
 */
class ConnectionManager {
public:
    /**
     * @brief Construct a ConnectionManager
     * 
     * @param config Server configuration with timeout and resource limits
     */
    explicit ConnectionManager(const ServerConfig* config);

    /**
     * @brief Destructor
     * 
     * Automatically removes all connections on destruction.
     */
    ~ConnectionManager();

    // Delete copy operations (ConnectionManager is not copyable)
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;

    // Delete move operations (config reference cannot be reseated)
    ConnectionManager(ConnectionManager&&) = delete;
    ConnectionManager& operator=(ConnectionManager&&) = delete;

    // =========================================================================
    // Connection Registration
    // =========================================================================

    /**
     * @brief Register a new connection
     * 
     * Creates a new Connection object with the given file descriptor and
     * assigns it a unique connection ID. The connection is stored in the
     * manager and can be looked up by its file descriptor.
     * 
     * @param fd The file descriptor for the new connection
     * @return std::shared_ptr<Connection> The newly created connection, or nullptr if limit exceeded
     */
    std::shared_ptr<Connection> register_connection(int fd);

    /**
     * @brief Remove a connection by file descriptor
     * 
     * Removes the connection with the given file descriptor from the manager.
     * The connection object will be destroyed when no other references exist.
     * 
     * @param fd The file descriptor of the connection to remove
     * @return true if the connection was found and removed, false otherwise
     */
    bool remove_connection(int fd);

    /**
     * @brief Remove a connection by pointer
     * 
     * Removes the given connection from the manager by its file descriptor.
     * 
     * @param conn The connection to remove
     * @return true if the connection was found and removed, false otherwise
     */
    bool remove_connection(const Connection* conn);

    // =========================================================================
    // Connection Lookup
    // =========================================================================

    /**
     * @brief Look up a connection by file descriptor
     * 
     * Returns a shared pointer to the connection with the given file descriptor.
     * If the connection is not found, returns nullptr.
     * 
     * @param fd The file descriptor to look up
     * @return std::shared_ptr<Connection> The connection, or nullptr if not found
     */
    std::shared_ptr<Connection> get_connection(int fd);

    /**
     * @brief Look up a connection by connection ID (Stage 5 - WorkerPool Integration)
     * 
     * Returns a shared pointer to the connection with the given ID.
     * If the connection is not found, returns nullptr.
     * This is used for worker completion handling where workers only have connection IDs.
     * 
     * @param connection_id The connection ID to look up
     * @return std::shared_ptr<Connection> The connection, or nullptr if not found
     */
    std::shared_ptr<Connection> get_connection_by_id(uint64_t connection_id);

    /**
     * @brief Check if a connection exists
     * 
     * @param fd The file descriptor to check
     * @return true if the connection exists, false otherwise
     */
    bool has_connection(int fd) const;

    // =========================================================================
    // Connection Statistics
    // =========================================================================

    /**
     * @brief Get the number of active connections
     * 
     * @return size_t The number of currently active connections
     */
    size_t active_connection_count() const;

    /**
     * @brief Get the total number of connections created
     * 
     * @return uint64_t The total number of connections created since startup
     */
    uint64_t total_connection_count() const;

    /**
     * @brief Check if the connection limit has been reached
     * 
     * @return true if at maximum capacity, false otherwise
     */
    bool at_capacity() const;

    // =========================================================================
    // Timeout Enforcement
    // =========================================================================

    /**
     * @brief Sweep inactive connections based on timeout policies
     * 
     * Iterates through all active connections and removes those that have
     * exceeded their timeout thresholds (header, body, write, keep-alive).
     * 
     * @return size_t The number of connections removed due to timeout
     */
    size_t sweep_timeouts();

    /**
     * @brief Remove a specific connection due to timeout
     * 
     * @param fd The file descriptor of the connection to remove
     * @param reason The timeout reason (for logging)
     * @return true if the connection was found and removed, false otherwise
     */
    bool timeout_connection(int fd, const std::string& reason);

    // =========================================================================
    // Connection Lifecycle
    // =========================================================================

    /**
     * @brief Remove all connections
     * 
     * Removes all active connections from the manager. This is typically
     * called during graceful shutdown.
     * 
     * @return size_t The number of connections removed
     */
    size_t remove_all();

    // =========================================================================
    // WebSocket Maintenance (Phase 23)
    // =========================================================================

    /**
     * @brief Queue keepalive Ping frames for upgraded connections
     *
     * Scans active WebSocket sessions and, for each one whose ping interval
     * has elapsed, queues a Ping frame and collects the file descriptor so
     * the caller can arm EPOLLOUT for the flush.
     *
     * @param ping_interval_ms Configured server ping interval (0 = disabled)
     * @return std::vector<int> File descriptors with freshly queued output
     */
    std::vector<int> maintain_websockets(uint64_t ping_interval_ms);

    /**
     * @brief Best-effort WebSocket Close handshake on server shutdown
     *
     * Queues a Close frame (going away) for every upgraded connection and
     * attempts a bounded nonblocking flush. Called after the event loop has
     * stopped, so no further reads are expected; the peer's reply is not
     * awaited.
     *
     * @param code Close status code to send (1001 = going away)
     * @return size_t Number of WebSocket connections notified
     */
    size_t initiate_websocket_shutdown(uint16_t code);

private:
    // =========================================================================
    // Private Helper Methods
    // =========================================================================

    /**
     * @brief Generate a unique connection ID
     * 
     * @return uint64_t A unique connection ID
     */
    uint64_t generate_connection_id();

    /**
     * @brief Check if a connection has exceeded its timeout
     * 
     * @param conn The connection to check
     * @return true if the connection has timed out, false otherwise
     */
    bool is_timed_out(const Connection& conn) const;

    /**
     * @brief Get the timeout reason for a connection
     * 
     * @param conn The connection to check
     * @return std::string The timeout reason, or empty string if not timed out
     */
    std::string get_timeout_reason(const Connection& conn) const;

    // =========================================================================
    // Member Variables
    // =========================================================================

    mutable std::mutex mutex_;  // Protects all member variables
    std::unordered_map<int, std::shared_ptr<Connection>> connections_;  // Active connections keyed by fd
    const ServerConfig* config_;  // Server configuration (pointer, not owned)
    uint64_t next_connection_id_;  // Next connection ID to assign
    uint64_t total_connections_;  // Total connections created
};

} // namespace aevrix
