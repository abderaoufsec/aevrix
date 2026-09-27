// =============================================================================
// Aevrix - HTTP Cache Control Utilities
// =============================================================================
// This header provides utilities for HTTP cache control features including
// ETag generation, HTTP date formatting, and conditional request validation.
//
// Features:
// - ETag generation (MD5 hash-based for strong validators)
// - HTTP date formatting (RFC 7231)
// - Conditional request validation (If-None-Match, If-Modified-Since)
// - Range request parsing
//
// Phase 19 Implementation:
// - ETag generation for cache validation
// - Last-Modified date formatting
// - Conditional request support
// - Range request support
//
// References:
// - RFC 7232: HTTP/1.1 ETag
// - RFC 7231: HTTP/1.1 Date Formats
// - RFC 7233: HTTP/1.1 Range Requests
// =============================================================================

#pragma once

#include <string>
#include <cstdint>
#include <ctime>

namespace aevrix {
namespace http {

// =============================================================================
// HTTP Date Utilities
// =============================================================================
// Functions for formatting and parsing HTTP dates according to RFC 7231.
// =============================================================================

/**
 * @brief Format a time_t as an HTTP date string
 * 
 * Formats the time according to RFC 7231 IMF-fixdate format:
 * "Wed, 21 Oct 2015 07:28:00 GMT"
 * 
 * @param time The time to format (seconds since epoch)
 * @return std::string The formatted HTTP date string
 */
std::string format_http_date(time_t time);

/**
 * @brief Get the current time as an HTTP date string
 * 
 * @return std::string The current time formatted as an HTTP date
 */
std::string current_http_date();

/**
 * @brief Parse an HTTP date string to time_t
 * 
 * Parses RFC 7231 IMF-fixdate format: "Wed, 21 Oct 2015 07:28:00 GMT"
 * 
 * @param date_str The HTTP date string to parse
 * @return time_t The parsed time (seconds since epoch), or -1 on error
 */
time_t parse_http_date(const std::string& date_str);

// =============================================================================
// ETag Utilities
// =============================================================================
// Functions for generating and validating ETags for cache control.
// =============================================================================

/**
 * @brief Generate a strong ETag from content
 * 
 * Generates an ETag based on the MD5 hash of the content.
 * This provides a strong validator that changes if the content changes.
 * 
 * @param content The content to generate an ETag for
 * @return std::string The generated ETag (hexadecimal string without quotes)
 */
std::string generate_etag(const std::string& content);

/**
 * @brief Generate a weak ETag from content
 * 
 * Generates a weak ETag (prefixed with W/) based on the MD5 hash.
 * Weak validators are used when the resource may change without being
// semantically different (e.g., server-side processing).
 * 
 * @param content The content to generate an ETag for
 * @return std::string The generated weak ETag (W/"hexadecimal string")
 */
std::string generate_weak_etag(const std::string& content);

/**
 * @brief Check if an ETag matches another (accounting for weak validators)
 * 
 * @param etag1 The first ETag
 * @param etag2 The second ETag
 * @return true if the ETags match, false otherwise
 */
bool etag_matches(const std::string& etag1, const std::string& etag2);

/**
 * @brief Parse ETag to check if it's weak
 * 
 * @param etag The ETag to check
 * @return true if the ETag is weak (starts with W/), false otherwise
 */
bool is_weak_etag(const std::string& etag);

// =============================================================================
// Conditional Request Validation
// =============================================================================
// Functions for validating conditional requests (If-None-Match, If-Modified-Since).
// =============================================================================

/**
 * @brief Check if a conditional request should return 304 Not Modified
 * 
 * Compares the client's ETag (If-None-Match) with the server's current ETag.
 * Returns true if they match, indicating the resource hasn't changed.
 * 
 * @param if_none_match The If-None-Match header value from the request
 * @param current_etag The current ETag of the resource
 * @return true if 304 Not Modified should be returned, false otherwise
 */
bool should_return_not_modified_etag(const std::string& if_none_match, 
                                      const std::string& current_etag);

/**
 * @brief Check if a conditional request should return 304 Not Modified
 * 
 * Compares the client's modification date (If-Modified-Since) with the server's
 * current modification date. Returns true if the resource hasn't been modified.
 * 
 * @param if_modified_since The If-Modified-Since header value from the request
 * @param last_modified The current Last-Modified time of the resource
 * @return true if 304 Not Modified should be returned, false otherwise
 */
bool should_return_not_modified_date(const std::string& if_modified_since,
                                       time_t last_modified);

// =============================================================================
// Range Request Utilities
// =============================================================================
// Functions for parsing and handling Range requests (RFC 7233).
// =============================================================================

/**
 * @brief Parse a Range header value
 * 
 * Parses Range header format: "bytes=start-end" or "bytes=start-end,start-end"
 * 
 * @param range_header The Range header value
 * @param file_size The total size of the file
 * @param start_out Output parameter for the start byte offset
 * @param end_out Output parameter for the end byte offset
 * @return true if the range is valid, false otherwise
 */
bool parse_range_header(const std::string& range_header, size_t file_size,
                       size_t& start_out, size_t& end_out);

/**
 * @brief Format a Content-Range header value
 * 
 * Formats Content-Range header according to RFC 7233: "bytes start-end/total"
 * 
 * @param start The start byte offset
 * @param end The end byte offset
 * @param total The total file size
 * @return std::string The formatted Content-Range header value
 */
std::string format_content_range(size_t start, size_t end, size_t total);

} // namespace http
} // namespace aevrix
