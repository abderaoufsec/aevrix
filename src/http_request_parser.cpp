// =============================================================================
// Aevrix - HTTP Request Parser Implementation
// =============================================================================
// This file implements the HttpRequestParser class for parsing HTTP requests
// from raw bytes according to RFC 9112.
//
// Implementation Notes:
// - State machine-based parsing for robustness
// - Incremental parsing support for partial reads
// - Strict validation of request format
// - Protection against resource exhaustion via limits
// - Detailed error reporting for debugging
// =============================================================================

#include "aevrix/http_request_parser.h"
#include <algorithm>
#include <sstream>
#include <cctype>

namespace aevrix {
namespace http {

// =============================================================================
// Request Line Parsing
// =============================================================================

bool HttpRequestParser::parse_request_line() {
    // Wait until we have a complete line (CRLF)
    size_t crlf_pos = find_crlf();
    if (crlf_pos == std::string::npos) {
        if (buffer_.length() > config_.max_request_line_bytes) {
            set_error("Request line too long");
            return false;
        }
        return false;  // Need more data
    }

    // Extract the request line
    std::string request_line = consume_bytes(crlf_pos);
    consume_bytes(2);  // Consume the CRLF

    // Parse the request line: METHOD TARGET VERSION
    std::istringstream iss(request_line);
    std::string method_str, target, version;

    // Extract method
    if (!(iss >> method_str)) {
        set_error("Missing method in request line");
        return false;
    }

    // Extract target
    if (!(iss >> target)) {
        set_error("Missing target in request line");
        return false;
    }

    // Extract version
    if (!(iss >> version)) {
        set_error("Missing version in request line");
        return false;
    }

    // Validate and set method
    HttpMethod method = string_to_http_method(method_str);
    if (method == HttpMethod::HTTP_UNKNOWN) {
        set_error("Unknown HTTP method: " + method_str);
        return false;
    }
    request_.set_method(method);

    // Validate and set target
    if (target.empty()) {
        set_error("Empty target in request line");
        return false;
    }
    request_.set_target(target);

    // Validate and set version
    if (version.empty()) {
        set_error("Empty version in request line");
        return false;
    }
    // Check that version starts with "HTTP/"
    if (version.find("HTTP/") != 0) {
        set_error("Invalid HTTP version: " + version);
        return false;
    }
    request_.set_version(version);

    return true;
}

// =============================================================================
// Header Parsing
// =============================================================================

bool HttpRequestParser::parse_header() {
    // Wait until we have a complete line (CRLF)
    size_t crlf_pos = find_crlf();
    if (crlf_pos == std::string::npos) {
        if (buffer_.length() > config_.max_header_field_bytes) {
            set_error("Header field too long");
            return false;
        }
        return false;  // Need more data
    }

    // Check for empty line (end of headers)
    if (crlf_pos == 0) {
        return true;  // Will be handled by main loop
    }

    // Extract the header line
    std::string header_line = consume_bytes(crlf_pos);
    consume_bytes(2);  // Consume the CRLF

    // Check header count limit
    if (headers_received_ >= config_.max_header_count) {
        set_error("Too many headers");
        return false;
    }

    // Parse header: Field-Name: Field-Value
    size_t colon_pos = header_line.find(':');
    if (colon_pos == std::string::npos) {
        set_error("Invalid header format (missing colon): " + header_line);
        return false;
    }

    // Extract header name and value
    std::string name = header_line.substr(0, colon_pos);
    std::string value = header_line.substr(colon_pos + 1);

    // Trim whitespace from name and value
    // Note: HttpHeader constructor handles value trimming
    // We need to trim the name manually
    name.erase(0, name.find_first_not_of(" \t"));
    name.erase(name.find_last_not_of(" \t") + 1);

    // Validate header name
    HttpHeader test_header(name, "test");
    if (!test_header.is_valid_name()) {
        set_error("Invalid header name: " + name);
        return false;
    }

    // Check total header size limit
    total_header_bytes_ += header_line.length();
    if (total_header_bytes_ > config_.max_total_header_bytes) {
        set_error("Total header size exceeded");
        return false;
    }

    // Add header to request
    request_.set_header(name, value);
    headers_received_++;

    return true;
}

// =============================================================================
// Body Parsing
// =============================================================================

bool HttpRequestParser::parse_body() {
    // Check if we have a Content-Length header
    std::string content_length_str = request_.get_header("Content-Length");
    if (content_length_str.empty()) {
        // No Content-Length, assume no body
        return true;
    }

    // Parse Content-Length
    size_t content_length;
    try {
        content_length = std::stoul(content_length_str);
    } catch (...) {
        set_error("Invalid Content-Length: " + content_length_str);
        return false;
    }

    // Check body size limit
    if (content_length > config_.max_body_bytes) {
        set_error("Body too large");
        return false;
    }

    // Check if we have enough data
    if (buffer_.length() < content_length) {
        return false;  // Need more data
    }

    // Extract the body
    std::string body = consume_bytes(content_length);
    request_.set_body(body);

    return true;
}

} // namespace http
} // namespace aevrix
