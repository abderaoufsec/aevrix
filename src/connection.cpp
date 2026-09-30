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
#ifdef AEVRIX_ENABLE_TLS
#include "aevrix/tls_context.h"
#endif
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cerrno>
#include <string.h>  // For strerror on Linux
#include <errno.h>    // For errno on Linux

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

// Use http namespace for readability
using aevrix::http::HttpRequest;

namespace aevrix {

Connection::Connection(int fd, uint64_t id)
    : fd_(fd)
    , id_(id)
    , created_at_(std::chrono::steady_clock::now())
    , last_activity_(std::chrono::steady_clock::now())
    , deadline_(std::chrono::steady_clock::time_point::max())
    , worker_active_(false) {

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
// Nonblocking I/O Operations (Stage 1 - Event-Driven Refactor)
// =============================================================================

Connection::IoResult Connection::read_nonblocking() {
    constexpr size_t BUFFER_SIZE = 8192;
    char buffer[BUFFER_SIZE];

#ifdef AEVRIX_ENABLE_TLS
    // Phase 21: Use SSL_read() if TLS is enabled
    if (tls_enabled_ && tls_connection_) {
        size_t bytes_read = 0;
        TlsIoRequirement io_req = tls_connection_->read(buffer, BUFFER_SIZE, bytes_read);

        if (io_req == TlsIoRequirement::Closed) {
            return IoResult::Closed;
        }

        if (io_req == TlsIoRequirement::WantRead || io_req == TlsIoRequirement::WantWrite) {
            // TLS needs more I/O, wait for event loop
            return IoResult::InProgress;
        }

        if (bytes_read > 0) {
            // Append to input buffer
            input_buffer_.insert(input_buffer_.end(), buffer, buffer + bytes_read);
            return IoResult::Success;
        }

        return IoResult::InProgress;
    }
#endif

#ifdef _WIN32
    SOCKET sock = static_cast<SOCKET>(fd_);
    int received = recv(sock, buffer, static_cast<int>(BUFFER_SIZE), 0);

    if (received == SOCKET_ERROR) {
        int error = WSAGetLastError();

        // Handle nonblocking mode: WSAEWOULDBLOCK means no data available yet
        if (error == WSAEWOULDBLOCK) {
            // Not an error - just no data available right now
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_,
                                                 "read_nonblocking: no data available (WSAEWOULDBLOCK)");
            return IoResult::InProgress;
        }

        // Handle interrupted system call - retry
        if (error == WSAEINTR) {
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_,
                                                 "read_nonblocking: interrupted (WSAEINTR), will retry");
            return IoResult::InProgress;
        }

        // Real error
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::ERR, id_,
                                             "read_nonblocking failed: " + std::to_string(error));
        return IoResult::Error;
    }
    
    if (received == 0) {
        // Connection closed by peer
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_, 
                                             "read_nonblocking: peer closed connection");
        return IoResult::Closed;
    }
#else
    ssize_t received = recv(fd_, buffer, BUFFER_SIZE, 0);

    if (received < 0) {
        int error = errno;
        
        // Handle nonblocking mode: EAGAIN/EWOULDBLOCK means no data available yet
        if (error == EAGAIN || error == EWOULDBLOCK) {
            // Not an error - just no data available right now
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_, 
                                                 "read_nonblocking: no data available (EAGAIN/EWOULDBLOCK)");
            return IoResult::InProgress;
        }
        
        // Handle interrupted system call - retry
        if (error == EINTR) {
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_, 
                                                 "read_nonblocking: interrupted (EINTR), will retry");
            return IoResult::InProgress;
        }
        
        // Real error
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::ERR, id_, 
                                             "read_nonblocking failed: " + std::string(strerror(error)));
        return IoResult::Error;
    }
    
    if (received == 0) {
        // Connection closed by peer
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_, 
                                             "read_nonblocking: peer closed connection");
        return IoResult::Closed;
    }
#endif

    // Data received successfully - append to input buffer
    std::size_t old_size = input_buffer_.size();
    input_buffer_.resize(old_size + static_cast<std::size_t>(received));
    std::memcpy(input_buffer_.data() + old_size, buffer, static_cast<std::size_t>(received));
    
    aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_, 
                                         "read_nonblocking: received " + std::to_string(received) + " bytes");
    
    // Update activity timestamp
    update_activity();
    
    return IoResult::Success;
}

Connection::IoResult Connection::write_nonblocking() {
    if (output_buffer_.empty()) {
        // Nothing to write
        return IoResult::Success;
    }

    // Write from current offset
    size_t remaining = output_buffer_.size() - write_offset_;
    if (remaining == 0) {
        // Already written everything
        output_buffer_.clear();
        write_offset_ = 0;
        return IoResult::Success;
    }

#ifdef AEVRIX_ENABLE_TLS
    // Phase 21: Use SSL_write() if TLS is enabled
    if (tls_enabled_ && tls_connection_) {
        size_t bytes_written = 0;
        TlsIoRequirement io_req = tls_connection_->write(
            output_buffer_.data() + write_offset_, remaining, bytes_written);

        if (io_req == TlsIoRequirement::Closed) {
            return IoResult::Error;
        }

        if (io_req == TlsIoRequirement::WantRead || io_req == TlsIoRequirement::WantWrite) {
            // TLS needs more I/O, wait for event loop
            return IoResult::InProgress;
        }

        if (bytes_written > 0) {
            // Update write offset
            write_offset_ += bytes_written;

            if (write_offset_ >= output_buffer_.size()) {
                // Full write - clear buffer and reset offset
                output_buffer_.clear();
                write_offset_ = 0;
            }

            aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_,
                                                 "write_nonblocking (TLS): sent " + std::to_string(bytes_written) + " bytes, " +
                                                 std::to_string(output_buffer_.size() - write_offset_) + " bytes remaining");

            // Update activity timestamp
            update_activity();

            return IoResult::Success;
        }

        return IoResult::InProgress;
    }
#endif

#ifdef _WIN32
    SOCKET sock = static_cast<SOCKET>(fd_);
    int sent = send(sock, output_buffer_.data() + write_offset_,
                    static_cast<int>(remaining), 0);

    if (sent == SOCKET_ERROR) {
        int error = WSAGetLastError();

        // Handle nonblocking mode: WSAEWOULDBLOCK means send buffer full
        if (error == WSAEWOULDBLOCK) {
            // Not an error - just can't write right now
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_,
                                                 "write_nonblocking: send buffer full (WSAEWOULDBLOCK)");
            return IoResult::InProgress;
        }

        // Handle interrupted system call - retry
        if (error == WSAEINTR) {
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_,
                                                 "write_nonblocking: interrupted (WSAEINTR), will retry");
            return IoResult::InProgress;
        }

        // Real error
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::ERR, id_,
                                             "write_nonblocking failed: " + std::to_string(error));
        return IoResult::Error;
    }
#else
    ssize_t sent = send(fd_, output_buffer_.data() + write_offset_, remaining, 0);

    if (sent < 0) {
        int error = errno;

        // Handle nonblocking mode: EAGAIN/EWOULDBLOCK means send buffer full
        if (error == EAGAIN || error == EWOULDBLOCK) {
            // Not an error - just can't write right now
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_,
                                                 "write_nonblocking: send buffer full (EAGAIN/EWOULDBLOCK)");
            return IoResult::InProgress;
        }

        // Handle interrupted system call - retry
        if (error == EINTR) {
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_,
                                                 "write_nonblocking: interrupted (EINTR), will retry");
            return IoResult::InProgress;
        }

        // Real error
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::ERR, id_,
                                             "write_nonblocking failed: " + std::string(strerror(error)));
        return IoResult::Error;
    }
#endif

    // Update write offset (Stage 4 - more efficient than shifting)
    size_t sent_size = static_cast<size_t>(sent);
    write_offset_ += sent_size;

    if (write_offset_ >= output_buffer_.size()) {
        // Full write - clear buffer and reset offset
        output_buffer_.clear();
        write_offset_ = 0;
    }

    aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, id_,
                                         "write_nonblocking: sent " + std::to_string(sent) + " bytes, " +
                                         std::to_string(output_buffer_.size() - write_offset_) + " bytes remaining");

    // Update activity timestamp
    update_activity();

    return IoResult::Success;
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

// =============================================================================
// Deadline Management (Stage 6 - Timeout Enforcement)
// =============================================================================

void Connection::set_deadline(const ServerConfig& config) {
    // If worker is active, don't set deadline (worker has its own timeout)
    if (worker_active_) {
        deadline_ = std::chrono::steady_clock::time_point::max();
        return;
    }

    auto now = std::chrono::steady_clock::now();
    uint64_t timeout_ms = 0;

    // Determine timeout based on current state
#ifdef AEVRIX_ENABLE_TLS
    // Phase 21: TLS handshake gets header timeout
    if (state_ == ConnectionState::TlsHandshake) {
        timeout_ms = config.header_timeout_ms();
    } else
#endif
    // Phase 23: upgraded connections leave the HTTP timeout states behind.
    // Pending writes use the write budget (the 101 response and frame
    // flushes), an in-flight close handshake uses the close budget, and an
    // established but idle session is bounded by the keep-alive timeout
    // (server pings, when enabled, keep it alive).
    if (websocket_) {
        if (write_state_ == WriteState::Body || write_state_ == WriteState::Headers) {
            timeout_ms = config.write_timeout_ms();
        } else if (websocket_->state() == ws::WebSocketState::Closing) {
            timeout_ms = config.websocket_close_timeout_ms();
        } else {
            timeout_ms = config.keep_alive_timeout_ms();
        }
    } else if (read_state_ == ReadState::Headers) {
        timeout_ms = config.header_timeout_ms();
    } else if (read_state_ == ReadState::Body) {
        timeout_ms = config.body_timeout_ms();
    } else if (write_state_ == WriteState::Body || write_state_ == WriteState::Headers) {
        timeout_ms = config.write_timeout_ms();
    } else if (state_ == ConnectionState::Waiting) {
        timeout_ms = config.keep_alive_timeout_ms();
    } else {
        // No timeout for other states
        deadline_ = std::chrono::steady_clock::time_point::max();
        return;
    }

    deadline_ = now + std::chrono::milliseconds(timeout_ms);
}

bool Connection::has_deadline_exceeded() const {
    // If worker is active, don't timeout
    if (worker_active_) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    if (deadline_ == std::chrono::steady_clock::time_point::max()) {
        return false;  // No deadline set
    }

    if (now >= deadline_) {
        return true;
    }

    return false;
}

// =============================================================================
// TLS Support (Phase 21)
// =============================================================================

#ifdef AEVRIX_ENABLE_TLS
void Connection::init_tls(TlsContext& ctx) {
    try {
        tls_connection_ = std::make_unique<TlsConnection>(ctx, fd_);
        tls_enabled_ = true;
        g_logger.log_with_connection(aevrix::LogLevel::INFO, id_, "TLS initialized for connection");
    } catch (const std::exception& e) {
        g_logger.log_with_connection(aevrix::LogLevel::ERR, id_, "Failed to initialize TLS: " + std::string(e.what()));
        throw;
    }
}

bool Connection::is_handshake_complete() const {
    return tls_connection_ && tls_connection_->is_handshake_complete();
}
#endif

} // namespace aevrix
