// =============================================================================
// Aevrix - HTTP Method Enumeration
// =============================================================================
// This header defines HTTP methods as per RFC 9110 (HTTP Semantics).
// HTTP methods indicate the desired action to be performed on the target resource.
//
// Common HTTP Methods:
// - GET: Request a representation of the target resource
// - HEAD: Request the headers only (no body)
// - POST: Submit data to the target resource
// - PUT: Replace the target resource with the request data
// - DELETE: Delete the target resource
// - OPTIONS: Request information about communication options
// - PATCH: Apply partial modifications to the target resource
//
// Phase 4 Implementation:
// We'll implement the most common methods needed for basic HTTP server:
// - GET, HEAD (required per roadmap)
// - POST, PUT, DELETE (common web server methods)
// - OPTIONS, PATCH (additional useful methods)
//
// Reference: RFC 9110 - https://httpwg.org/specs/rfc9110.html
// =============================================================================

#pragma once

#include <string>
#include <cstdint>
#include <algorithm>

namespace aevrix {
namespace http {

// =============================================================================
// HTTP Method Enumeration
// =============================================================================
// Defines the standard HTTP methods as per RFC 9110.
// Using an enum class provides type safety and prevents invalid methods.
// =============================================================================
enum class HttpMethod : uint8_t {
    /**
     * GET
     * Request a representation of the target resource.
     * The GET method requests a representation of the specified resource.
     * Requests using GET should only retrieve data.
     */
    GET = 0,

    /**
     * HEAD
     * Request the headers only (no body).
     * The HEAD method asks for a response identical to a GET request,
     * but without the response body. Useful for retrieving metadata.
     */
    HEAD = 1,

    /**
     * POST
     * Submit data to the target resource.
     * The POST method is used to submit an entity to the specified resource,
     // often causing a change in state or side effects on the server.
     */
    POST = 2,

    /**
     * PUT
     * Replace the target resource with the request data.
     * The PUT method replaces all current representations of the target
     * resource with the request payload.
     */
    PUT = 3,

    /**
     * DELETE
     * Delete the target resource.
     * The DELETE method deletes the specified resource.
     */
    HTTP_DELETE = 4,

    /**
     * OPTIONS
     * Request information about communication options.
     * The OPTIONS method describes the communication options for the target resource.
     */
    OPTIONS = 5,

    /**
     * PATCH
     * Apply partial modifications to the target resource.
     * The PATCH method applies partial modifications to a resource.
     */
    HTTP_PATCH = 6,

    /**
     * UNKNOWN
     * Represents an unknown or unsupported HTTP method.
     */
    HTTP_UNKNOWN = 255
};

// =============================================================================
// HTTP Method Utilities
// =============================================================================

/**
 * @brief Convert HTTP method to string
 * 
 * Returns the string representation of the HTTP method.
 * 
 * @param method The HTTP method enum
 * @return std::string The method string (e.g., "GET", "POST")
 */
inline std::string http_method_to_string(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET: return "GET";
        case HttpMethod::HEAD: return "HEAD";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::HTTP_DELETE: return "DELETE";
        case HttpMethod::OPTIONS: return "OPTIONS";
        case HttpMethod::HTTP_PATCH: return "PATCH";
        case HttpMethod::HTTP_UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Convert string to HTTP method
 * 
 * Parses a string and returns the corresponding HTTP method.
// Case-insensitive comparison is used.
 * 
 * @param method_str The method string (e.g., "GET", "get")
 * @return HttpMethod The corresponding HTTP method, or UNKNOWN if not recognized
 */
inline HttpMethod string_to_http_method(const std::string& method_str) {
    // Case-insensitive comparison
    std::string upper_method = method_str;
    std::transform(upper_method.begin(), upper_method.end(), 
                   upper_method.begin(), 
                   [](unsigned char c) { return std::toupper(c); });

    if (upper_method == "GET") return HttpMethod::GET;
    if (upper_method == "HEAD") return HttpMethod::HEAD;
    if (upper_method == "POST") return HttpMethod::POST;
    if (upper_method == "PUT") return HttpMethod::PUT;
    if (upper_method == "DELETE") return HttpMethod::HTTP_DELETE;
    if (upper_method == "OPTIONS") return HttpMethod::OPTIONS;
    if (upper_method == "PATCH") return HttpMethod::HTTP_PATCH;
    
    return HttpMethod::HTTP_UNKNOWN;
}

/**
 * @brief Check if a method is safe (no side effects)
 * 
 * Safe methods are those that are defined as having no side effects
// on the server. GET, HEAD, OPTIONS, and TRACE are considered safe.
 * 
 * @param method The HTTP method
 * @return true if the method is safe, false otherwise
 */
inline bool is_safe_method(HttpMethod method) {
    return method == HttpMethod::GET || 
           method == HttpMethod::HEAD || 
           method == HttpMethod::OPTIONS;
}

/**
 * @brief Check if a method is idempotent
 * 
// Idempotent methods can be called multiple times with the same effect.
// GET, HEAD, PUT, DELETE, OPTIONS, and TRACE are idempotent.
 * 
 * @param method The HTTP method
 * @return true if the method is idempotent, false otherwise
 */
inline bool is_idempotent_method(HttpMethod method) {
    return method == HttpMethod::GET || 
           method == HttpMethod::HEAD || 
           method == HttpMethod::PUT || 
           method == HttpMethod::HTTP_DELETE || 
           method == HttpMethod::OPTIONS;
}

/**
 * @brief Check if a method requires a request body
 * 
 * Some methods typically require a request body (POST, PUT, PATCH).
 * 
 * @param method The HTTP method
 * @return true if the method typically requires a body, false otherwise
 */
inline bool method_requires_body(HttpMethod method) {
    return method == HttpMethod::POST || 
           method == HttpMethod::PUT || 
           method == HttpMethod::HTTP_PATCH;
}

} // namespace http
} // namespace aevrix
