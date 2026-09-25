// =============================================================================
// Aevrix - Connection State Machine
// =============================================================================
// This file implements the connection state machine for the Aevrix HTTP server.
// In Phase 9, we formalize the connection state to make event-driven behavior
// explicit and eliminate scattered "magic boolean" connection states.
//
// The connection state machine tracks:
// - ConnectionState: Overall connection lifecycle
// - ReadState: Reading state for incoming data
// - WriteState: Writing state for outgoing data
// - TimeoutState: Connection timeout state
//
// Each connection knows:
// - input buffer: Received data waiting to be parsed
// - parser state: HTTP request parser progress
// - output buffer: Data waiting to be sent
// - current response: Response being constructed
// - keep-alive decision: Whether to keep connection alive
// - timestamps: Connection lifecycle timing
// - request id: Unique identifier for logging
//
// State Transitions:
// New → ReadingHeaders → ReadingBody → WritingResponse → KeepAlive/Close
//                                  ↓
//                              Error → Close
//
// Previous Phases:
// - Phase 8: Non-blocking I/O with epoll (Linux) / select (Windows)
//
// Future Phases Will Add:
// - Phase 10: Timeouts and resource limits
// =============================================================================

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <cstdint>
#include "aevrix/http_request_parser.h"
#include "aevrix/http_request.h"
#include "aevrix/http_response.h"

// Forward declaration for ServerConfig
namespace aevrix {
class ServerConfig;
}

namespace aevrix {

// Use http namespace for readability
using http::HttpRequestParser;
using http::HttpRequest;
using http::HttpResponse;
using http::StatusCode;

/**
 * @brief Overall connection lifecycle state
 * 
 * Tracks the high-level state of a connection through its lifecycle.
 */
enum class ConnectionState {
    New,           // Connection just accepted, not yet initialized
    Reading,       // Actively reading data from the client
    Writing,       // Actively writing data to the client
    Waiting,       // Waiting for next event (idle state in keep-alive)
    Closing,       // Connection is being closed
    Closed         // Connection is closed
};

/**
 * @brief Reading state for incoming data
 * 
 * Tracks what the connection is currently reading from the client.
 */
enum class ReadState {
    Idle,          // Not currently reading
    Headers,       // Reading HTTP request headers
    Body,          // Reading HTTP request body
    Complete,      // Finished reading the request
    Error          // Error occurred while reading
};

/**
 * @brief Writing state for outgoing data
 * 
 * Tracks what the connection is currently writing to the client.
 */
enum class WriteState {
    Idle,          // Not currently writing
    Headers,       // Writing HTTP response headers
    Body,          // Writing HTTP response body
    Complete,      // Finished writing the response
    Error          // Error occurred while writing
};

/**
 * @brief Timeout state for connection management
 * 
 * Tracks timeout status (used in Phase 10 for timeout enforcement).
 */
enum class TimeoutState {
    None,          // No timeout
    HeaderTimeout, // Timeout while reading headers
    BodyTimeout,   // Timeout while reading body
    WriteTimeout,  // Timeout while writing response
    KeepAliveTimeout  // Timeout during keep-alive idle
};

/**
 * @brief Connection class managing all connection state
 * 
 * This class encapsulates all state related to a single TCP connection,
// providing a clean interface for the event loop to manage connections
// without scattered "magic boolean" states.
 * 
 * The connection maintains:
// - Input buffer for received data
// - HTTP request parser state
// - Output buffer for data to send
// - Current HTTP response
// - Keep-alive decision logic
// - Timestamps for lifecycle tracking
// - Unique request ID for logging
 * 
 * This formalizes the connection state machine and makes event-driven
// behavior explicit and testable.
 */
class Connection {
public:
    /**
     * @brief Construct a new connection
     * 
     * @param fd The socket file descriptor for this connection
     * @param id Unique connection identifier
     */
    explicit Connection(int fd, uint64_t id);

    /**
     * @brief Destructor
     */
    ~Connection() = default;

    // Delete copy operations (connections are unique)
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Allow move operations
    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept = default;

    // =========================================================================
    // State Accessors
    // =========================================================================

    /**
     * @brief Get the overall connection state
     */
    ConnectionState state() const { return state_; }

    /**
     * @brief Set the overall connection state
     */
    void set_state(ConnectionState state) { state_ = state; }

    /**
     * @brief Get the reading state
     */
    ReadState read_state() const { return read_state_; }

    /**
     * @brief Set the reading state
     */
    void set_read_state(ReadState state) { read_state_ = state; }

    /**
     * @brief Get the writing state
     */
    WriteState write_state() const { return write_state_; }

    /**
     * @brief Set the writing state
     */
    void set_write_state(WriteState state) { write_state_ = state; }

    /**
     * @brief Get the timeout state
     */
    TimeoutState timeout_state() const { return timeout_state_; }

    /**
     * @brief Set the timeout state
     */
    void set_timeout_state(TimeoutState state) { timeout_state_ = state; }

    // =========================================================================
    // Buffer Management
    // =========================================================================

    /**
     * @brief Get the input buffer (received data)
     */
    const std::vector<char>& input_buffer() const { return input_buffer_; }

    /**
     * @brief Get mutable reference to input buffer
     */
    std::vector<char>& input_buffer() { return input_buffer_; }

    /**
     * @brief Get the output buffer (data to send)
     */
    const std::vector<char>& output_buffer() const { return output_buffer_; }

    /**
     * @brief Get mutable reference to output buffer
     */
    std::vector<char>& output_buffer() { return output_buffer_; }

    /**
     * @brief Clear the input buffer
     */
    void clear_input_buffer() { input_buffer_.clear(); }

    /**
     * @brief Clear the output buffer
     */
    void clear_output_buffer() { output_buffer_.clear(); }

    // =========================================================================
    // Parser State
    // =========================================================================

    /**
     * @brief Get the HTTP request parser
     */
    http::HttpRequestParser& parser() { return parser_; }

    /**
     * @brief Get the HTTP request parser (const)
     */
    const http::HttpRequestParser& parser() const { return parser_; }

    /**
     * @brief Check if the parser has completed parsing
     */
    bool is_request_complete() const { return parser_.is_complete(); }

    /**
     * @brief Check if the parser encountered an error
     */
    bool has_parse_error() const { return parser_.has_error(); }

    // =========================================================================
    // Response Management
    // =========================================================================

    /**
     * @brief Get the current response being sent
     */
    const http::HttpResponse& current_response() const { return current_response_; }

    /**
     * @brief Set the current response to send
     */
    void set_current_response(const http::HttpResponse& response) { current_response_ = response; }

    /**
     * @brief Clear the current response
     */
    void clear_current_response() { current_response_ = HttpResponse(); }

    // =========================================================================
    // Keep-Alive Management
    // =========================================================================

    /**
     * @brief Check if the connection should be kept alive
     */
    bool keep_alive() const { return keep_alive_; }

    /**
     * @brief Set whether to keep the connection alive
     */
    void set_keep_alive(bool keep_alive) { keep_alive_ = keep_alive; }

    /**
     * @brief Determine if keep-alive is appropriate based on the request
     * 
     * @param request The parsed HTTP request
     */
    void evaluate_keep_alive(const http::HttpRequest& request);

    // =========================================================================
    // Timestamp Management
    // =========================================================================

    /**
     * @brief Get the connection creation timestamp
     */
    const std::chrono::steady_clock::time_point& created_at() const { return created_at_; }

    /**
     * @brief Get the last activity timestamp
     */
    const std::chrono::steady_clock::time_point& last_activity() const { return last_activity_; }

    /**
     * @brief Update the last activity timestamp to now
     */
    void update_activity() { last_activity_ = std::chrono::steady_clock::now(); }

    /**
     * @brief Get the time since last activity
     */
    std::chrono::milliseconds time_since_activity() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - last_activity_);
    }

    /**
     * @brief Get the connection age
     */
    std::chrono::milliseconds age() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - created_at_);
    }

    // =========================================================================
    // Timeout Checking (Phase 10)
    // =========================================================================

    /**
     * @brief Check if header timeout has been exceeded
     * 
     * @param config Server configuration with timeout values
     * @return true if timeout exceeded, false otherwise
     */
    bool has_header_timeout(const ServerConfig& config) const;

    /**
     * @brief Check if body timeout has been exceeded
     * 
     * @param config Server configuration with timeout values
     * @return true if timeout exceeded, false otherwise
     */
    bool has_body_timeout(const ServerConfig& config) const;

    /**
     * @brief Check if keep-alive timeout has been exceeded
     * 
     * @param config Server configuration with timeout values
     * @return true if timeout exceeded, false otherwise
     */
    bool has_keep_alive_timeout(const ServerConfig& config) const;

    /**
     * @brief Check if write timeout has been exceeded
     * 
     * @param config Server configuration with timeout values
     * @return true if timeout exceeded, false otherwise
     */
    bool has_write_timeout(const ServerConfig& config) const;

    // =========================================================================
    // Connection Identifiers
    // =========================================================================

    /**
     * @brief Get the socket file descriptor
     */
    int fd() const { return fd_; }

    /**
     * @brief Get the unique connection ID
     */
    uint64_t id() const { return id_; }

    /**
     * @brief Get the request ID (increments per request)
     */
    uint64_t request_id() const { return request_id_; }

    /**
     * @brief Increment the request ID
     */
    void increment_request_id() { ++request_id_; }

    // =========================================================================
    // State Validation
    // =========================================================================

    /**
     * @brief Check if the connection is in a valid state
     */
    bool is_valid() const {
        return fd_ >= 0 && state_ != ConnectionState::Closed;
    }

    /**
     * @brief Check if the connection can accept new data
     */
    bool can_read() const {
        return state_ == ConnectionState::Reading || state_ == ConnectionState::Waiting;
    }

    /**
     * @brief Check if the connection can send data
     */
    bool can_write() const {
        return state_ == ConnectionState::Writing || state_ == ConnectionState::Waiting;
    }

    /**
     * @brief Check if the connection should be closed
     */
    bool should_close() const {
        return state_ == ConnectionState::Closing || 
               state_ == ConnectionState::Closed ||
               timeout_state_ != TimeoutState::None ||
               !keep_alive_;
    }

private:
    // =========================================================================
    // State Variables
    // =========================================================================

    ConnectionState state_ = ConnectionState::New;
    ReadState read_state_ = ReadState::Idle;
    WriteState write_state_ = WriteState::Idle;
    TimeoutState timeout_state_ = TimeoutState::None;

    // =========================================================================
    // Connection Identifiers
    // =========================================================================

    int fd_;                    // Socket file descriptor
    uint64_t id_;              // Unique connection ID
    uint64_t request_id_ = 0;  // Request counter (increments per request)

    // =========================================================================
    // Buffers
    // =========================================================================

    std::vector<char> input_buffer_;   // Received data waiting to be parsed
    std::vector<char> output_buffer_;  // Data waiting to be sent

    // =========================================================================
    // HTTP State
    // =========================================================================

    http::HttpRequestParser parser_;         // HTTP request parser
    http::HttpResponse current_response_;     // Current response being sent

    // =========================================================================
    // Connection Policy
    // =========================================================================

    bool keep_alive_ = false;  // Whether to keep connection alive

    // =========================================================================
    // Timestamps
    // =========================================================================

    std::chrono::steady_clock::time_point created_at_;      // When connection was created
    std::chrono::steady_clock::time_point last_activity_;  // Last I/O activity
};

} // namespace aevrix
