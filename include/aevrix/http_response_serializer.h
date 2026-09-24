// =============================================================================
// Aevrix - HTTP Response Serializer
// =============================================================================
// This header provides a serializer for converting HttpResponse objects to
// the raw HTTP wire format (bytes that can be sent over the network).
//
// HTTP Response Wire Format (RFC 9112):
// status-line CRLF
// *(field-line CRLF)
// CRLF
// [message-body]
//
// Example:
// HTTP/1.1 200 OK\r\n
// Content-Type: text/plain\r\n
// Content-Length: 13\r\n
// Connection: keep-alive\r\n
// \r\n
// Hello, World!
//
// Key Features:
// - Converts structured HttpResponse to wire format
// - Proper CRLF line endings (\r\n)
// - Efficient string building
// - Validation of serialized output
// - Support for all status codes and headers
//
// Phase 3 Implementation:
// - Basic serialization of status line, headers, and body
// - Proper line ending handling
// - Connection header management
// - Content-Length handling
// =============================================================================

#pragma once

#include "aevrix/http_response.h"
#include <string>
#include <sstream>

namespace aevrix {
namespace http {

// =============================================================================
// HttpResponseSerializer Class
// =============================================================================
// Serializes HttpResponse objects to the HTTP wire format.
// Ensures proper formatting according to RFC 9112.
// =============================================================================
class HttpResponseSerializer {
public:
    // =========================================================================
    // Serialization Methods
    // =========================================================================

    /**
     * @brief Serialize an HTTP response to wire format
     * 
     * Converts an HttpResponse object to the raw HTTP wire format
     * that can be sent over the network.
     * 
     * Serialization Process:
     * 1. Build status line: "HTTP/1.1 200 OK\r\n"
     * 2. Append all headers: "Content-Type: text/plain\r\n"
     * 3. Add empty line to separate headers from body: "\r\n"
     * 4. Append body content (if any)
     * 
     * @param response The HTTP response to serialize
     * @return std::string The serialized response in wire format
     * 
     * @note The response is automatically validated before serialization.
     *       If the response is invalid, an empty string is returned.
     */
    static std::string serialize(const HttpResponse& response);

    /**
     * @brief Serialize only the status line
     * 
     * Serializes just the status line (used for special cases).
     * 
     * @param response The HTTP response
     * @return std::string The serialized status line
     */
    static std::string serialize_status_line(const HttpResponse& response);

    /**
     * @brief Serialize only the headers
     * 
     * Serializes just the headers section (used for special cases).
     * 
     * @param response The HTTP response
     * @return std::string The serialized headers
     */
    static std::string serialize_headers(const HttpResponse& response);

    // =========================================================================
    // Validation Methods
    // =========================================================================

    /**
     * @brief Validate a serialized response
     * 
     * Checks that the serialized response conforms to HTTP format:
     * - Contains status line
     * - Has proper CRLF line endings
     * - Has empty line between headers and body
     * 
     * @param serialized The serialized response string
     * @return true if the format is valid, false otherwise
     */
    static bool validate_serialized(const std::string& serialized);

private:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr const char* CRLF = "\r\n";  ///< HTTP line ending
    static constexpr const char* HEADER_SEPARATOR = ": ";  ///< Header name/value separator

    // =========================================================================
    // Private Helper Methods
    // =========================================================================

    /**
     * @brief Build the status line
     * 
     * Format: "HTTP/1.1 200 OK"
     * 
     * @param response The HTTP response
     * @return std::string The status line
     */
    static std::string build_status_line(const HttpResponse& response);

    /**
     * @brief Build a single header line
     * 
     * Format: "Content-Type: text/plain"
     * 
     * @param header The header to serialize
     * @return std::string The serialized header line
     */
    static std::string build_header_line(const HttpHeader& header);
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline std::string HttpResponseSerializer::serialize(const HttpResponse& response) {
    // Validate the response before serialization
    if (!response.is_valid()) {
        return "";
    }

    std::ostringstream oss;

    // 1. Serialize status line
    oss << build_status_line(response) << CRLF;

    // 2. Serialize all headers
    for (const auto& header : response.headers()) {
        oss << build_header_line(header) << CRLF;
    }

    // 3. Add empty line to separate headers from body
    oss << CRLF;

    // 4. Append body (if any)
    if (response.has_body()) {
        oss << response.body();
    }

    return oss.str();
}

inline std::string HttpResponseSerializer::serialize_status_line(const HttpResponse& response) {
    return build_status_line(response) + CRLF;
}

inline std::string HttpResponseSerializer::serialize_headers(const HttpResponse& response) {
    std::ostringstream oss;
    
    for (const auto& header : response.headers()) {
        oss << build_header_line(header) << CRLF;
    }
    
    // Add empty line after headers
    oss << CRLF;
    
    return oss.str();
}

inline std::string HttpResponseSerializer::build_status_line(const HttpResponse& response) {
    std::ostringstream oss;
    oss << response.version() << " "
        << status_code_to_int(response.status()) << " "
        << status_code_to_string(response.status());
    return oss.str();
}

inline std::string HttpResponseSerializer::build_header_line(const HttpHeader& header) {
    std::ostringstream oss;
    oss << header.name() << HEADER_SEPARATOR << header.value();
    return oss.str();
}

inline bool HttpResponseSerializer::validate_serialized(const std::string& serialized) {
    // Basic validation: check that we have at least a status line
    if (serialized.empty()) {
        return false;
    }

    // Check for HTTP version prefix
    if (serialized.find("HTTP/") != 0) {
        return false;
    }

    // Check for status line ending
    size_t status_end = serialized.find(CRLF);
    if (status_end == std::string::npos) {
        return false;
    }

    // Check for empty line between headers and body
    size_t empty_line = serialized.find(std::string(CRLF) + CRLF);
    if (empty_line == std::string::npos) {
        return false;
    }

    return true;
}

} // namespace http
} // namespace aevrix
