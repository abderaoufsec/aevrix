// =============================================================================
// Aevrix - HTTP Request Class
// =============================================================================
// This header provides a class for representing HTTP requests.
// An HTTP request consists of:
// - Request line (method, target, HTTP version)
// - Headers (metadata about the request)
// - Body (optional content)
//
// HTTP Request Format (RFC 9112):
// request-line CRLF
// *(field-line CRLF)
// CRLF
// [message-body]
//
// Example:
// GET /index.html HTTP/1.1
// Host: example.com
// User-Agent: Aevrix/0.1.0
//
// (no body for GET)
//
// Key Features:
// - Structured representation of HTTP request components
// - Method, target, and version management
// - Header integration with HttpHeaders class
// - Body content handling
// - Request validation
//
// Phase 4 Implementation:
// - Basic request structure with method, target, version, headers, and body
// - Request validation
// - Integration with HTTP parser
// =============================================================================

#pragma once

#include "aevrix/http_method.h"
#include "aevrix/http_header.h"
#include <string>
#include <cstdint>
#include <sstream>

namespace aevrix {
namespace http {

// =============================================================================
// HttpRequest Class
// =============================================================================
// Represents a complete HTTP request with request line, headers, and body.
// Provides methods for building and manipulating requests.
// =============================================================================
class HttpRequest {
public:
    // =========================================================================
    // Constructors
    // =========================================================================

    /**
     * @brief Default constructor - creates a basic GET request
     * 
     * Creates a request with:
     * - Method: GET
     * - Target: "/"
     * - HTTP version: HTTP/1.1
     * - Empty headers and body
     */
    HttpRequest();

    /**
     * @brief Constructor with method and target
     * 
     * Creates a request with the specified method and target.
     * 
     * @param method The HTTP method
     * @param target The request target (path)
     */
    HttpRequest(HttpMethod method, const std::string& target);

    /**
     * @brief Constructor with method, target, and body
     * 
     * Creates a request with the specified method, target, and body.
     * 
     * @param method The HTTP method
     * @param target The request target (path)
     * @param body The request body content
     */
    HttpRequest(HttpMethod method, const std::string& target, const std::string& body);

    /**
     * @brief Constructor with all components
     * 
     * Creates a complete request with all components.
     * 
     * @param method The HTTP method
     * @param target The request target (path)
     * @param version The HTTP version
     * @param headers The request headers
     * @param body The request body content
     */
    HttpRequest(HttpMethod method, const std::string& target, const std::string& version,
                const HttpHeaders& headers, const std::string& body);

    // =========================================================================
    // Request Line Management
    // =========================================================================

    /**
     * @brief Set the HTTP method
     * 
     * @param method The HTTP method
     */
    void set_method(HttpMethod method) {
        method_ = method;
    }

    /**
     * @brief Get the HTTP method
     * 
     * @return HttpMethod The current method
     */
    HttpMethod method() const {
        return method_;
    }

    /**
     * @brief Set the request target
     * 
     * @param target The request target (path)
     */
    void set_target(const std::string& target) {
        target_ = target;
    }

    /**
     * @brief Get the request target
     * 
     * @return const std::string& The request target
     */
    const std::string& target() const {
        return target_;
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
     * @brief Get the request headers
     * 
     * @return HttpHeaders& Reference to the headers collection
     */
    HttpHeaders& headers() {
        return headers_;
    }

    /**
     * @brief Get the request headers (const)
     * 
     * @return const HttpHeaders& Const reference to the headers collection
     */
    const HttpHeaders& headers() const {
        return headers_;
    }

    /**
     * @brief Set a header
     * 
     * Convenience method to set a header on the request.
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
     * @brief Set the request body
     * 
     * @param body The request body content
     */
    void set_body(const std::string& body) {
        body_ = body;
    }

    /**
     * @brief Get the request body
     * 
     * @return const std::string& The request body content
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

    /**
     * @brief Check if the request has a body
     * 
     * @return true if the body is non-empty, false otherwise
     */
    bool has_body() const {
        return !body_.empty();
    }

    /**
     * @brief Check if the method typically requires a body
     * 
     * @return true if the method typically requires a body, false otherwise
     */
    bool requires_body() const {
        return method_ == HttpMethod::POST || 
               method_ == HttpMethod::PUT || 
               method_ == HttpMethod::HTTP_PATCH;
    }

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Reset the request to default state
     * 
     * Resets the request to:
     * - Method: GET
     * - Target: "/"
     * - Version: HTTP/1.1
     * - Empty headers
     * - Empty body
     */
    void reset();

    /**
     * @brief Validate the request
     * 
     * Performs basic validation of the request:
     * - Method is valid
     * - Target is not empty
     * - Version is valid
     * - All headers are valid
     * 
     * @return true if the request is valid, false otherwise
     */
    bool is_valid() const;

    /**
     * @brief Get the request line as a string
     * 
     * Returns the request line in the format: "GET / HTTP/1.1"
     * 
     * @return std::string The request line string
     */
    std::string request_line() const;

private:
    // =========================================================================
    // Member Variables
    // =========================================================================

    HttpMethod method_;        ///< HTTP method (GET, POST, etc.)
    std::string target_;      ///< Request target (path)
    std::string version_;     ///< HTTP version (e.g., "HTTP/1.1")
    HttpHeaders headers_;    ///< Request headers
    std::string body_;        ///< Request body content
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline HttpRequest::HttpRequest()
    : method_(HttpMethod::GET),
      target_("/"),
      version_("HTTP/1.1") {
}

inline HttpRequest::HttpRequest(HttpMethod method, const std::string& target)
    : method_(method),
      target_(target),
      version_("HTTP/1.1") {
}

inline HttpRequest::HttpRequest(HttpMethod method, const std::string& target, 
                                const std::string& body)
    : method_(method),
      target_(target),
      version_("HTTP/1.1"),
      body_(body) {
}

inline HttpRequest::HttpRequest(HttpMethod method, const std::string& target, 
                                const std::string& version, const HttpHeaders& headers,
                                const std::string& body)
    : method_(method),
      target_(target),
      version_(version),
      headers_(headers),
      body_(body) {
}

inline void HttpRequest::reset() {
    method_ = HttpMethod::GET;
    target_ = "/";
    version_ = "HTTP/1.1";
    headers_.clear();
    body_.clear();
}

inline bool HttpRequest::is_valid() const {
    // Check that method is not UNKNOWN
    if (method_ == HttpMethod::HTTP_UNKNOWN) {
        return false;
    }

    // Check that target is not empty
    if (target_.empty()) {
        return false;
    }

    // Check that version is not empty
    if (version_.empty()) {
        return false;
    }

    // Check that all headers are valid
    for (const auto& header : headers_) {
        if (!header.is_valid()) {
            return false;
        }
    }

    return true;
}

inline std::string HttpRequest::request_line() const {
    std::ostringstream oss;
    oss << http_method_to_string(method_) << " " << target_ << " " << version_;
    return oss.str();
}

} // namespace http
} // namespace aevrix
