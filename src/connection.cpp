// =============================================================================
// Aevrix - Connection State Machine Implementation
// =============================================================================
// This file implements the connection state machine for managing HTTP connection
// lifecycle and state in an event-driven server.
// =============================================================================

#include "aevrix/connection.h"
#include "aevrix/http_request_parser.h"
#include "aevrix/http_request.h"
#include "aevrix/http_response.h"
#include "aevrix/server_config.h"
#include <iostream>
#include <algorithm>

// Use http namespace for readability
using aevrix::http::HttpRequest;

namespace aevrix {

Connection::Connection(int fd, uint64_t id)
    : fd_(fd)
    , id_(id)
    , created_at_(std::chrono::steady_clock::now())
    , last_activity_(std::chrono::steady_clock::now()) {
    
    std::cout << "Connection " << id_ << " created (fd=" << fd_ << ")\n";
}

void Connection::evaluate_keep_alive(const HttpRequest& request) {
    // Check HTTP version - HTTP/1.1 defaults to keep-alive
    if (request.version() == "HTTP/1.1") {
        // Check for explicit "Connection: close" header
        std::string connection_header = request.headers().get("Connection");
        if (!connection_header.empty()) {
            // Case-insensitive comparison
            std::string lower = connection_header;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "close") {
                keep_alive_ = false;
                std::cout << "Connection " << id_ << ": keep-alive disabled (Connection: close)\n";
            } else {
                keep_alive_ = true;
                std::cout << "Connection " << id_ << ": keep-alive enabled (HTTP/1.1 default)\n";
            }
        } else {
            keep_alive_ = true;
            std::cout << "Connection " << id_ << ": keep-alive enabled (HTTP/1.1 default)\n";
        }
    } else {
        // HTTP/1.0 defaults to close unless "Connection: keep-alive"
        std::string connection_header = request.headers().get("Connection");
        if (!connection_header.empty()) {
            std::string lower = connection_header;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "keep-alive") {
                keep_alive_ = true;
                std::cout << "Connection " << id_ << ": keep-alive enabled (HTTP/1.0 explicit)\n";
            } else {
                keep_alive_ = false;
                std::cout << "Connection " << id_ << ": keep-alive disabled (HTTP/1.0 default)\n";
            }
        } else {
            keep_alive_ = false;
            std::cout << "Connection " << id_ << ": keep-alive disabled (HTTP/1.0 default)\n";
        }
    }
}

// =============================================================================
// Timeout Checking (Phase 10)
// =============================================================================

bool Connection::has_header_timeout(const ServerConfig& config) const {
    if (read_state_ != ReadState::Headers) {
        return false;  // Only check when reading headers
    }
    
    auto elapsed = time_since_activity();
    if (elapsed.count() > static_cast<int64_t>(config.header_timeout_ms())) {
        std::cout << "Connection " << id_ << ": header timeout (" 
                  << elapsed.count() << " ms > " << config.header_timeout_ms() << " ms)\n";
        return true;
    }
    
    return false;
}

bool Connection::has_body_timeout(const ServerConfig& config) const {
    if (read_state_ != ReadState::Body) {
        return false;  // Only check when reading body
    }
    
    auto elapsed = time_since_activity();
    if (elapsed.count() > static_cast<int64_t>(config.body_timeout_ms())) {
        std::cout << "Connection " << id_ << ": body timeout (" 
                  << elapsed.count() << " ms > " << config.body_timeout_ms() << " ms)\n";
        return true;
    }
    
    return false;
}

bool Connection::has_keep_alive_timeout(const ServerConfig& config) const {
    if (state_ != ConnectionState::Waiting) {
        return false;  // Only check when in keep-alive waiting state
    }
    
    auto elapsed = time_since_activity();
    if (elapsed.count() > static_cast<int64_t>(config.keep_alive_timeout_ms())) {
        std::cout << "Connection " << id_ << ": keep-alive timeout (" 
                  << elapsed.count() << " ms > " << config.keep_alive_timeout_ms() << " ms)\n";
        return true;
    }
    
    return false;
}

bool Connection::has_write_timeout(const ServerConfig& config) const {
    if (write_state_ != WriteState::Body && write_state_ != WriteState::Headers) {
        return false;  // Only check when writing
    }
    
    auto elapsed = time_since_activity();
    if (elapsed.count() > static_cast<int64_t>(config.write_timeout_ms())) {
        std::cout << "Connection " << id_ << ": write timeout (" 
                  << elapsed.count() << " ms > " << config.write_timeout_ms() << " ms)\n";
        return true;
    }
    
    return false;
}

} // namespace aevrix
