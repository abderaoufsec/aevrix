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

} // namespace aevrix
