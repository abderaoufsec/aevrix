// =============================================================================
// Aevrix - Connection Manager Implementation
// =============================================================================
// This file implements the ConnectionManager class for managing active client
// connections in the event-driven HTTP server.
// =============================================================================

#include "aevrix/connection_manager.h"
#include "aevrix/logger.h"
#include "aevrix/server_config_store.h"
#include <algorithm>
#include <vector>

namespace aevrix {

ConnectionManager::ConnectionManager(const ServerConfig* config, const ServerConfigStore* store)
    : config_(config)
    , store_(store)
    , next_connection_id_(1)
    , total_connections_(0) {
    
    aevrix::g_logger.info("ConnectionManager initialized with max_connections=" + 
                         std::to_string(config_view()->max_connections()) +
                         (store_ != nullptr ? " (live: re-read after every config reload)" : ""));
}

std::shared_ptr<const ServerConfig> ConnectionManager::config_view() const {
    if (store_ != nullptr) {
        // Atomic snapshot: immutable for as long as this manager uses it, and a
        // concurrent reload cannot tear the values it reads.
        return store_->snapshot();
    }
    // Startup-only configuration: alias the borrowed object without owning it.
    return std::shared_ptr<const ServerConfig>(std::shared_ptr<const ServerConfig>{}, config_);
}

ConnectionManager::~ConnectionManager() {
    size_t removed = remove_all();
    aevrix::g_logger.info("ConnectionManager destroyed, removed " + 
                         std::to_string(removed) + " connections");
}

// =============================================================================
// Connection Registration
// =============================================================================

std::shared_ptr<Connection> ConnectionManager::register_connection(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if we're at capacity
    if (connections_.size() >= static_cast<size_t>(config_view()->max_connections())) {
        aevrix::g_logger.warn("ConnectionManager at capacity (" + 
                            std::to_string(connections_.size()) + "/" + 
                            std::to_string(config_view()->max_connections()) + 
                            "), rejecting new connection fd=" + std::to_string(fd));
        return nullptr;
    }
    
    // Check if fd already exists (shouldn't happen, but defensive)
    if (connections_.find(fd) != connections_.end()) {
        aevrix::g_logger.warn("Connection fd=" + std::to_string(fd) + 
                            " already exists in ConnectionManager");
        return nullptr;
    }
    
    // Generate unique connection ID
    uint64_t connection_id = generate_connection_id();
    
    // Create new connection
    auto conn = std::make_shared<Connection>(fd, connection_id);
    
    // Store connection
    connections_[fd] = conn;
    total_connections_++;
    
    aevrix::g_logger.log_with_connection(aevrix::LogLevel::INFO, connection_id,
                                         "Registered new connection (fd=" + std::to_string(fd) + 
                                         ", total=" + std::to_string(connections_.size()) + ")");
    
    return conn;
}

bool ConnectionManager::remove_connection(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return false;
    }
    
    uint64_t connection_id = it->second->id();
    connections_.erase(it);
    
    aevrix::g_logger.log_with_connection(aevrix::LogLevel::INFO, connection_id,
                                         "Removed connection (fd=" + std::to_string(fd) + 
                                         ", remaining=" + std::to_string(connections_.size()) + ")");
    
    return true;
}

bool ConnectionManager::remove_connection(const Connection* conn) {
    if (!conn) {
        return false;
    }
    
    return remove_connection(conn->fd());
}

// =============================================================================
// Connection Lookup
// =============================================================================

std::shared_ptr<Connection> ConnectionManager::get_connection(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return nullptr;
    }
    
    return it->second;
}

std::shared_ptr<Connection> ConnectionManager::get_connection_by_id(uint64_t connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Linear search by ID (Stage 5 - could be optimized with a secondary map if needed)
    for (const auto& [fd, conn] : connections_) {
        if (conn->id() == connection_id) {
            return conn;
        }
    }
    
    return nullptr;
}

bool ConnectionManager::has_connection(int fd) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    return connections_.find(fd) != connections_.end();
}

// =============================================================================
// Connection Statistics
// =============================================================================

size_t ConnectionManager::active_connection_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    return connections_.size();
}

uint64_t ConnectionManager::total_connection_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    return total_connections_;
}

bool ConnectionManager::at_capacity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    return connections_.size() >= static_cast<size_t>(config_view()->max_connections());
}

// =============================================================================
// Timeout Enforcement
// =============================================================================

size_t ConnectionManager::sweep_timeouts() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t removed = 0;
    
    // Collect timed-out connections
    std::vector<int> timed_out_fds;
    for (const auto& [fd, conn] : connections_) {
        if (is_timed_out(*conn)) {
            std::string reason = get_timeout_reason(*conn);
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::WARN, conn->id(),
                                                 "Connection timed out: " + reason);
            timed_out_fds.push_back(fd);
        }
    }
    
    // Remove timed-out connections
    // Note: This only removes from ConnectionManager. The socket is closed by
    // the Connection destructor, which will trigger EPOLLHUP in the event loop.
    // The event loop callback will then remove the fd from epoll.
    for (int fd : timed_out_fds) {
        auto it = connections_.find(fd);
        if (it != connections_.end()) {
            uint64_t connection_id = it->second->id();
            connections_.erase(it);
            removed++;

            aevrix::g_logger.log_with_connection(aevrix::LogLevel::INFO, connection_id,
                                                 "Removed timed-out connection (fd=" + std::to_string(fd) + ")");
        }
    }
    
    if (removed > 0) {
        aevrix::g_logger.info("Sweep removed " + std::to_string(removed) + 
                             " timed-out connections (remaining=" + 
                             std::to_string(connections_.size()) + ")");
    }
    
    return removed;
}

bool ConnectionManager::timeout_connection(int fd, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return false;
    }
    
    uint64_t connection_id = it->second->id();
    connections_.erase(it);
    
    aevrix::g_logger.log_with_connection(aevrix::LogLevel::WARN, connection_id,
                                         "Connection timed out: " + reason);
    
    return true;
}

// =============================================================================
// Connection Lifecycle
// =============================================================================

size_t ConnectionManager::remove_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t count = connections_.size();
    connections_.clear();
    
    aevrix::g_logger.info("Removed all connections (" + std::to_string(count) + ")");
    
    return count;
}

// =============================================================================
// WebSocket Maintenance (Phase 23)
// =============================================================================

std::vector<int> ConnectionManager::maintain_websockets(uint64_t ping_interval_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<int> fds;
    for (auto& entry : connections_) {
        Connection* conn = entry.second.get();
        auto* ws_session = conn->websocket();
        if (ws_session == nullptr) {
            continue;
        }
        if (ws_session->maybe_queue_ping(ping_interval_ms)) {
            conn->append_output_buffer(ws_session->take_output());
            conn->set_write_state(WriteState::Body);
            conn->set_deadline(*config_);
            fds.push_back(entry.first);
        }
    }
    return fds;
}

size_t ConnectionManager::initiate_websocket_shutdown(uint16_t code) {
    std::lock_guard<std::mutex> lock(mutex_);

    size_t notified = 0;
    for (auto& entry : connections_) {
        Connection* conn = entry.second.get();
        auto* ws_session = conn->websocket();
        if (ws_session == nullptr) {
            continue;
        }

        ws_session->initiate_close(code);
        conn->append_output_buffer(ws_session->take_output());

        // Best-effort flush: the event loop has already stopped, so drain
        // whatever the nonblocking socket accepts before the fds are closed.
        while (conn->has_pending_output()) {
            const Connection::IoResult result = conn->write_nonblocking();
            if (result != Connection::IoResult::Success) {
                break;
            }
        }

        aevrix::g_logger.log_with_connection(aevrix::LogLevel::INFO, conn->id(),
                                             "WebSocket close frame queued for shutdown");
        ++notified;
    }
    return notified;
}

// =============================================================================
// Private Helper Methods
// =============================================================================

uint64_t ConnectionManager::generate_connection_id() {
    return next_connection_id_++;
}

bool ConnectionManager::is_timed_out(const Connection& conn) const {
    // Stage 6: Use deadline-based timeout checking
    // This is more accurate than activity-based checking and respects worker activity
    return conn.has_deadline_exceeded();
}

std::string ConnectionManager::get_timeout_reason(const Connection& conn) const {
#ifdef AEVRIX_ENABLE_TLS
    // Phase 21: a connection stuck in the TLS handshake state is bounded by
    // the header timeout budget (see Connection::set_deadline).
    if (conn.state() == ConnectionState::TlsHandshake) {
        return "TLS handshake timeout";
    }
#endif

    if (conn.has_header_timeout(*config_)) {
        return "header timeout";
    }
    
    if (conn.has_body_timeout(*config_)) {
        return "body timeout";
    }
    
    if (conn.has_write_timeout(*config_)) {
        return "write timeout";
    }
    
    if (conn.has_keep_alive_timeout(*config_)) {
        return "keep-alive timeout";
    }

    // Phase 23: upgraded connections report WebSocket-specific reasons.
    if (conn.is_websocket()) {
        return conn.websocket()->state() == ws::WebSocketState::Closing
                   ? "WebSocket close handshake timeout"
                   : "WebSocket idle timeout";
    }

    return "unknown";
}

} // namespace aevrix
