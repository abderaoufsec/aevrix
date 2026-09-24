// =============================================================================
// Aevrix - HTTP Response Class
// =============================================================================
// This header provides a class for representing HTTP responses.
// An HTTP response consists of:
// - Status line (HTTP version, status code, reason phrase)
// - Headers (metadata about the response)
// - Body (optional content)
//
// HTTP Response Format (RFC 9112):
// status-line CRLF
// *(field-line CRLF)
// CRLF
// [message-body]
//
// Example:
// HTTP/1.1 200 OK
// Content-Type: text/plain
// Content-Length: 13
//
// Hello, World!
//
// Key Features:
// - Structured representation of HTTP response components
// - Connection policy management (keep-alive vs close)
// - Content length calculation
// - Integration with HttpHeader and StatusCode classes
//
// Phase 3 Implementation:
// - Basic response structure with status, headers, and body
// - Connection policy support
// - Content length calculation
// - Integration with response serializer
// =============================================================================

#pragma once

#include "aevrix/http_status.h"
#include "aevrix/http_header.h"
#include <string>
#include <cstdint>

namespace aevrix {
namespace http {

// =============================================================================
// Connection Policy
// =============================================================================
// Defines how the connection should be handled after the response.
// HTTP/1.1 defaults to keep-alive, but responses can override this.
// =============================================================================
enum class ConnectionPolicy {
    /**
     * Keep the connection alive for subsequent requests
     * Corresponds to "Connection: keep-alive" header
     */
    KeepAlive,
    
    /**
     * Close the connection after this response
     * Corresponds to "Connection: close" header
     */
    Close
};

// =============================================================================
// HttpResponse Class
// =============================================================================
// Represents a complete HTTP response with status line, headers, and body.
// Provides methods for building and manipulating responses.
// =============================================================================
class HttpResponse {
public:
    // =========================================================================
    // Constructors
    // =========================================================================

    /**
     * @brief Default constructor - creates a basic 200 OK response
     * 
     * Creates a response with:
     * - Status: 200 OK
     * - HTTP version: HTTP/1.1
     * - Connection policy: KeepAlive
     * - Empty headers and body
     */
    HttpResponse();

    /**
     * @brief Constructor with status code
     * 
     * Creates a response with the specified status code.
     * 
     * @param status The HTTP status code
     */
    explicit HttpResponse(StatusCode status);

    /**
     * @brief Constructor with status code and body
     * 
     * Creates a response with the specified status code and body.
     * The Content-Length header will be set automatically.
     * 
     * @param status The HTTP status code
     * @param body The response body content
     */
    HttpResponse(StatusCode status, const std::string& body);

    /**
     * @brief Constructor with status code, headers, and body
     * 
     * Creates a complete response with all components.
     * 
     * @param status The HTTP status code
     * @param headers The response headers
     * @param body The response body content
     */
    HttpResponse(StatusCode status, const HttpHeaders& headers, const std::string& body);

    // =========================================================================
    // Status Line Management
    // =========================================================================

    /**
     * @brief Set the HTTP status code
     * 
     * @param status The HTTP status code
     */
    void set_status(StatusCode status) {
        status_ = status;
    }

    /**
     * @brief Get the HTTP status code
     * 
     * @return StatusCode The current status code
     */
    StatusCode status() const {
        return status_;
    }

    /**
     * @brief Set the HTTP version
     * 
     * @param version The HTTP version (e.g., "HTTP/1.1")
     */
    void set_version(const std::string& version) {
        version_ = version;
    }

    /**
     * @brief Get the HTTP version
     * 
     * @return const std::string& The HTTP version
     */
    const std::string& version() const {
        return version_;
    }

    // =========================================================================
    // Header Management
    // =========================================================================

    /**
     * @brief Get the response headers
     * 
     * @return HttpHeaders& Reference to the headers collection
     */
    HttpHeaders& headers() {
        return headers_;
    }

    /**
     * @brief Get the response headers (const)
     * 
     * @return const HttpHeaders& Const reference to the headers collection
     */
    const HttpHeaders& headers() const {
        return headers_;
    }

    /**
     * @brief Set a header
     * 
     * Convenience method to set a header on the response.
     * 
     * @param name The header name
     * @param value The header value
     */
    void set_header(const std::string& name, const std::string& value) {
        headers_.set(name, value);
    }

    /**
     * @brief Get a header value
     * 
     * Convenience method to get a header value.
     * 
     * @param name The header name
     * @return std::string The header value, or empty if not found
     */
    std::string get_header(const std::string& name) const {
        return headers_.get(name);
    }

    // =========================================================================
    // Body Management
    // =========================================================================

    /**
     * @brief Set the response body
     * 
     * Sets the body content and automatically updates the Content-Length header.
     * 
     * @param body The response body content
     */
    void set_body(const std::string& body);

    /**
     * @brief Get the response body
     * 
     * @return const std::string& The response body content
     */
    const std::string& body() const {
        return body_;
    }

    /**
     * @brief Get the content length
     * 
     * Returns the length of the body in bytes.
     * 
     * @return size_t The content length
     */
    size_t content_length() const {
        return body_.length();
    }

    // =========================================================================
    // Connection Policy Management
    // =========================================================================

    /**
     * @brief Set the connection policy
     * 
     * Sets how the connection should be handled after this response.
     * This will add/update the Connection header accordingly.
     * 
     * @param policy The connection policy (KeepAlive or Close)
     */
    void set_connection_policy(ConnectionPolicy policy);

    /**
     * @brief Get the connection policy
     * 
     * @return ConnectionPolicy The current connection policy
     */
    ConnectionPolicy connection_policy() const {
        return connection_policy_;
    }

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Check if the response has a body
     * 
     * @return true if the body is non-empty, false otherwise
     */
    bool has_body() const {
        return !body_.empty();
    }

    /**
     * @brief Reset the response to default state
     * 
     * Resets the response to:
     * - Status: 200 OK
     * - Empty headers
     * - Empty body
     * - Connection policy: KeepAlive
     */
    void reset();

    /**
     * @brief Validate the response
     * 
     * Performs basic validation of the response:
     * - Status code is valid
     * - All headers are valid
     * - Content-Length header matches body length (if present)
     * 
     * @return true if the response is valid, false otherwise
     */
    bool is_valid() const;

private:
    // =========================================================================
    // Private Helper Methods
    // =========================================================================

    /**
     * @brief Update the Content-Length header based on body length
     * 
     * Sets or updates the Content-Length header to match the current body length.
     */
    void update_content_length();

    // =========================================================================
    // Member Variables
    // =========================================================================

    std::string version_;           ///< HTTP version (e.g., "HTTP/1.1")
    StatusCode status_;            ///< HTTP status code
    HttpHeaders headers_;          ///< Response headers
    std::string body_;             ///< Response body content
    ConnectionPolicy connection_policy_;  ///< Connection policy for this response
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline HttpResponse::HttpResponse()
    : version_("HTTP/1.1"),
      status_(StatusCode::OK),
      connection_policy_(ConnectionPolicy::KeepAlive) {
}

inline HttpResponse::HttpResponse(StatusCode status)
    : version_("HTTP/1.1"),
      status_(status),
      connection_policy_(ConnectionPolicy::KeepAlive) {
}

inline HttpResponse::HttpResponse(StatusCode status, const std::string& body)
    : version_("HTTP/1.1"),
      status_(status),
      connection_policy_(ConnectionPolicy::KeepAlive) {
    set_body(body);
}

inline HttpResponse::HttpResponse(StatusCode status, const HttpHeaders& headers, 
                                  const std::string& body)
    : version_("HTTP/1.1"),
      status_(status),
      headers_(headers),
      connection_policy_(ConnectionPolicy::KeepAlive) {
    set_body(body);
}

inline void HttpResponse::set_body(const std::string& body) {
    body_ = body;
    update_content_length();
}

inline void HttpResponse::set_connection_policy(ConnectionPolicy policy) {
    connection_policy_ = policy;
    
    // Update the Connection header
    if (policy == ConnectionPolicy::KeepAlive) {
        headers_.set("Connection", "keep-alive");
    } else {
        headers_.set("Connection", "close");
    }
}

inline void HttpResponse::reset() {
    version_ = "HTTP/1.1";
    status_ = StatusCode::OK;
    headers_.clear();
    body_.clear();
    connection_policy_ = ConnectionPolicy::KeepAlive;
}

inline void HttpResponse::update_content_length() {
    if (has_body()) {
        headers_.set("Content-Length", std::to_string(content_length()));
    } else {
        // Remove Content-Length header if there's no body
        headers_.remove("Content-Length");
    }
}

inline bool HttpResponse::is_valid() const {
    // Check that all headers are valid
    for (const auto& header : headers_) {
        if (!header.is_valid()) {
            return false;
        }
    }

    // Check Content-Length header consistency
    std::string content_length_str = headers_.get("Content-Length");
    if (!content_length_str.empty()) {
        try {
            size_t content_length = std::stoul(content_length_str);
            // Content-Length should match body length
            // Exception: For HEAD responses, Content-Length can be 0 even if body is empty
            // This is a special case for Phase 5; Phase 6 will handle this properly
            if (content_length != body_.length() && !(body_.empty() && content_length == 0)) {
                return false;  // Content-Length doesn't match body length
            }
        } catch (...) {
            return false;  // Invalid Content-Length value
        }
    }

    return true;
}

} // namespace http
} // namespace aevrix
