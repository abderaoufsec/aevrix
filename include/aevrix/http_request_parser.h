// =============================================================================
// Aevrix - HTTP Request Parser
// =============================================================================
// This header provides an incremental HTTP request parser that converts raw bytes
// into structured HttpRequest objects. The parser handles the complexities of
// HTTP/1.1 request parsing according to RFC 9112.
//
// HTTP Request Format (RFC 9112):
// request-line CRLF
// *(field-line CRLF)
// CRLF
// [message-body]
//
// Example:
// GET /index.html HTTP/1.1\r\n
// Host: example.com\r\n
// User-Agent: Aevrix/0.1.0\r\n
// \r\n
// (body for POST/PUT)
//
// Key Features:
// - Incremental parsing (handles partial reads)
// - State machine-based parsing
// - Strict RFC 9112 compliance
// - Configurable limits (header size, body size, etc.)
// - Detailed error reporting
// - Protection against malformed requests
//
// Parser States:
// - RequestLine: Parsing the method, target, and version
// - Headers: Parsing header fields
// - Body: Reading the message body (if present)
// - Complete: Request fully parsed
// - Error: Parsing error occurred
//
// Phase 4 Implementation:
// - Basic request line parsing
// - Header parsing with validation
// - Simple body parsing (Content-Length only)
// - Incremental parsing support
// - Configurable limits
// =============================================================================

#pragma once

#include "aevrix/http_request.h"
#include "aevrix/http_method.h"
#include "aevrix/http_header.h"
#include <string>
#include <cstdint>
#include <optional>
#include <sstream>

namespace aevrix {
namespace http {

// =============================================================================
// Parser State Enumeration
// =============================================================================
// Represents the current state of the HTTP request parser.
// The parser progresses through states as it processes the request.
// =============================================================================
enum class ParserState {
    /**
     * RequestLine: Currently parsing the request line
     * Format: METHOD TARGET VERSION
     * Example: GET /index.html HTTP/1.1
     */
    RequestLine,

    /**
     * Headers: Currently parsing header fields
     * Format: Field-Name: Field-Value
     * Example: Content-Type: text/plain
     */
    Headers,

    /**
     * Body: Currently reading the message body
     * Only present for methods that typically have bodies (POST, PUT, PATCH)
     */
    Body,

    /**
     * Complete: Request has been fully parsed successfully
     */
    Complete,

    /**
     * Error: A parsing error occurred
     * The parser cannot continue from this state.
     */
    Error
};

// =============================================================================
// Parser Configuration
// =============================================================================
// Configurable limits for the HTTP request parser to prevent resource exhaustion.
// These limits can be adjusted based on server requirements.
// =============================================================================
struct ParserConfig {
    /**
     * Maximum size of the request line in bytes
     * Prevents excessively long request lines from consuming memory.
     * Default: 8192 bytes (8KB)
     */
    size_t max_request_line_bytes = 8192;

    /**
     * Maximum size of a single header field in bytes
     * Prevents individual headers from consuming excessive memory.
     * Default: 8192 bytes (8KB)
     */
    size_t max_header_field_bytes = 8192;

    /**
     * Maximum number of header fields
     * Prevents header flooding attacks.
     * Default: 100 headers
     */
    size_t max_header_count = 100;

    /**
     * Maximum total size of all headers in bytes
     * Prevents overall header memory exhaustion.
     * Default: 65536 bytes (64KB)
     */
    size_t max_total_header_bytes = 65536;

    /**
     * Maximum size of the request body in bytes
     * Prevents body memory exhaustion.
     * Default: 1048576 bytes (1MB)
     */
    size_t max_body_bytes = 1048576;

    /**
     * Default configuration for standard web server use
     */
    static ParserConfig default_config() {
        return ParserConfig{};
    }
};

// =============================================================================
// HttpRequestParser Class
// =============================================================================
// Incremental HTTP request parser that converts raw bytes to HttpRequest objects.
// Uses a state machine to parse requests according to RFC 9112.
// =============================================================================
class HttpRequestParser {
public:
    // =========================================================================
    // Constructors and Destructor
    // =========================================================================

    /**
     * @brief Constructor with default configuration
     * 
     * Creates a parser with default limits suitable for most web servers.
     */
    HttpRequestParser();

    /**
     * @brief Constructor with custom configuration
     * 
     * Creates a parser with custom limits for specific use cases.
     * 
     * @param config The parser configuration
     */
    explicit HttpRequestParser(const ParserConfig& config);

    /**
     * @brief Destructor
     */
    ~HttpRequestParser() = default;

    // =========================================================================
    // Parser Methods
    // =========================================================================

    /**
     * @brief Feed bytes to the parser
     * 
     * Feeds raw bytes to the parser for incremental parsing.
     * The parser will process as much as possible and update its state.
     * 
     * @param data Pointer to the data bytes
     * @param length Number of bytes to feed
     * 
     * @note The parser may not consume all bytes if it's waiting for more data.
     *       Call feed() again with additional bytes when more data arrives.
     */
    void feed(const char* data, size_t length);

    /**
     * @brief Feed a string to the parser
     * 
     * Convenience method for feeding string data.
     * 
     * @param data The string data to feed
     */
    void feed(const std::string& data) {
        feed(data.c_str(), data.length());
    }

    /**
     * @brief Reset the parser for a new request
     * 
     * Resets the parser state and buffers to start parsing a new request.
     * Should be called after a request is complete or after an error.
     */
    void reset();

    // =========================================================================
    // State Accessors
    // =========================================================================

    /**
     * @brief Get the current parser state
     * 
     * @return ParserState The current state of the parser
     */
    ParserState state() const {
        return state_;
    }

    /**
     * @brief Check if parsing is complete
     * 
     * @return true if the request has been fully parsed, false otherwise
     */
    bool is_complete() const {
        return state_ == ParserState::Complete;
    }

    /**
     * @brief Check if an error occurred
     * 
     * @return true if a parsing error occurred, false otherwise
     */
    bool has_error() const {
        return state_ == ParserState::Error;
    }

    /**
     * @brief Get the error message
     * 
     * Returns a description of the parsing error, if any.
     * 
     * @return std::string The error message, or empty if no error
     */
    std::string error_message() const {
        return error_message_;
    }

    // =========================================================================
    // Result Accessors
    // =========================================================================

    /**
     * @brief Get the parsed request
     * 
     * Returns the parsed HttpRequest object.
     * Only valid when is_complete() returns true.
     * 
     * @return const HttpRequest& The parsed request
     * 
     * @note The request is only valid after parsing is complete.
     *       Check is_complete() before calling this method.
     */
    const HttpRequest& request() const {
        return request_;
    }

    /**
     * @brief Take the parsed request
     * 
     * Returns the parsed HttpRequest object and resets the parser.
     * Useful for moving the request out of the parser.
     * 
     * @return HttpRequest The parsed request
     * 
     * @note This method resets the parser state to start parsing a new request.
     */
    HttpRequest take_request();

private:
    // =========================================================================
    // Private Parsing Methods
    // =========================================================================

    /**
     * @brief Parse the request line
     * 
     * Parses the method, target, and version from the request line.
     * Format: METHOD TARGET VERSION
     * 
     * @return true if successful, false on error
     */
    bool parse_request_line();

    /**
     * @brief Parse a single header field
     * 
     * Parses a header field in the format: Field-Name: Field-Value
     * 
     * @return true if successful, false on error
     */
    bool parse_header();

    /**
     * @brief Parse the request body
     * 
     * Parses the message body based on Content-Length header.
     * 
     * @return true if successful, false on error
     */
    bool parse_body();

    /**
     * @brief Find a CRLF sequence in the buffer
     * 
     * Searches for "\r\n" in the current buffer.
     * 
     * @return size_t Position of CRLF, or std::string::npos if not found
     */
    size_t find_crlf() const;

    /**
     * @brief Consume bytes from the buffer
     * 
     * Removes and returns the specified number of bytes from the buffer.
     * 
     * @param count Number of bytes to consume
     * @return std::string The consumed bytes
     */
    std::string consume_bytes(size_t count);

    /**
     * @brief Set an error state
     * 
     * Sets the parser to error state with a descriptive message.
     * 
     * @param message The error message
     */
    void set_error(const std::string& message);

    // =========================================================================
    // Member Variables
    // =========================================================================

    ParserState state_;              ///< Current parser state
    ParserConfig config_;           ///< Parser configuration limits
    HttpRequest request_;           ///< The request being built
    std::string buffer_;           ///< Input buffer for incremental parsing
    std::string error_message_;    ///< Error message if in error state
    size_t headers_received_;      ///< Number of headers received so far
    size_t total_header_bytes_;    ///< Total bytes of headers received
    size_t body_bytes_received_;   ///< Number of body bytes received
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline HttpRequestParser::HttpRequestParser()
    : state_(ParserState::RequestLine),
      config_(ParserConfig::default_config()),
      headers_received_(0),
      total_header_bytes_(0),
      body_bytes_received_(0) {
}

inline HttpRequestParser::HttpRequestParser(const ParserConfig& config)
    : state_(ParserState::RequestLine),
      config_(config),
      headers_received_(0),
      total_header_bytes_(0),
      body_bytes_received_(0) {
}

inline void HttpRequestParser::feed(const char* data, size_t length) {
    if (has_error()) {
        return;  // Don't process data if already in error state
    }

    if (is_complete()) {
        reset();  // Auto-reset if feeding into a complete parser
    }

    // Append data to buffer
    buffer_.append(data, length);

    // Process as much as possible
    while (true) {
        if (state_ == ParserState::RequestLine) {
            if (!parse_request_line()) {
                return;  // Either error or need more data
            }
            state_ = ParserState::Headers;
        } else if (state_ == ParserState::Headers) {
            if (!parse_header()) {
                return;  // Either error or need more data
            }
            // Check if we've reached the empty line (end of headers)
            size_t crlf_pos = find_crlf();
            if (crlf_pos == 0) {
                // Empty line found, move to body or complete
                consume_bytes(2);  // Consume the CRLF
                if (request_.has_body() || request_.requires_body()) {
                    state_ = ParserState::Body;
                } else {
                    state_ = ParserState::Complete;
                }
            }
        } else if (state_ == ParserState::Body) {
            if (!parse_body()) {
                return;  // Either error or need more data
            }
            state_ = ParserState::Complete;
        } else if (state_ == ParserState::Complete) {
            return;  // Done processing
        } else if (state_ == ParserState::Error) {
            return;  // Error state, stop processing
        }
    }
}

inline void HttpRequestParser::reset() {
    state_ = ParserState::RequestLine;
    request_ = HttpRequest();
    buffer_.clear();
    error_message_.clear();
    headers_received_ = 0;
    total_header_bytes_ = 0;
    body_bytes_received_ = 0;
}

inline HttpRequest HttpRequestParser::take_request() {
    if (!is_complete()) {
        return HttpRequest();  // Return empty request if not complete
    }

    HttpRequest result = request_;
    reset();
    return result;
}

inline size_t HttpRequestParser::find_crlf() const {
    return buffer_.find("\r\n");
}

inline std::string HttpRequestParser::consume_bytes(size_t count) {
    if (count > buffer_.length()) {
        count = buffer_.length();
    }

    std::string result = buffer_.substr(0, count);
    buffer_.erase(0, count);
    return result;
}

inline void HttpRequestParser::set_error(const std::string& message) {
    state_ = ParserState::Error;
    error_message_ = message;
}

} // namespace http
} // namespace aevrix
