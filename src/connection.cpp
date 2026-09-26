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
#include "aevrix/logger.h"
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
    
    aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_, 
                                        "Connection created (fd=" + std::to_string(fd) + ")");
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
                aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_, 
                                                     "keep-alive disabled (Connection: close)");
            } else {
                keep_alive_ = true;
                aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_, 
                                                     "keep-alive enabled (HTTP/1.1 default)");
            }
        } else {
            keep_alive_ = true;
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_, 
                                                 "keep-alive enabled (HTTP/1.1 default)");
        }
    } else {
        // HTTP/1.0 defaults to close unless "Connection: keep-alive"
        std::string connection_header = request.headers().get("Connection");
        if (!connection_header.empty()) {
            std::string lower = connection_header;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "keep-alive") {
                keep_alive_ = true;
                aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_, 
                                                     "keep-alive enabled (HTTP/1.0 explicit)");
            } else {
                keep_alive_ = false;
                aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_, 
                                                     "keep-alive disabled (HTTP/1.0 default)");
            }
        } else {
            keep_alive_ = false;
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_, 
                                                 "keep-alive disabled (HTTP/1.0 default)");
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
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::WARN, id_, 
                                             "header timeout (" + std::to_string(elapsed.count()) + 
                                             " ms > " + std::to_string(config.header_timeout_ms()) + " ms)");
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
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::WARN, id_, 
                                             "body timeout (" + std::to_string(elapsed.count()) + 
                                             " ms > " + std::to_string(config.body_timeout_ms()) + " ms)");
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
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::WARN, id_, 
                                             "keep-alive timeout (" + std::to_string(elapsed.count()) + 
                                             " ms > " + std::to_string(config.keep_alive_timeout_ms()) + " ms)");
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
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::WARN, id_, 
                                             "write timeout (" + std::to_string(elapsed.count()) + 
                                             " ms > " + std::to_string(config.write_timeout_ms()) + " ms)");
        return true;
    }
    
    return false;
}

} // namespace aevrix
