// =============================================================================
// Aevrix - HTTP Status Codes
// =============================================================================
// This header defines HTTP status codes as per RFC 9110 (HTTP Semantics).
// Status codes are three-digit numbers that indicate the result of an HTTP request.
//
// Status Code Categories:
// - 1xx: Informational - Request received, continuing process
// - 2xx: Success - Request successfully received, understood, and accepted
// - 3xx: Redirection - Further action needed to complete the request
// - 4xx: Client Error - Request contains bad syntax or cannot be fulfilled
// - 5xx: Server Error - Server failed to fulfill valid request
//
// Phase 3 Implementation:
// We'll implement the most common status codes needed for basic HTTP server:
// - 200 OK, 400 Bad Request, 404 Not Found, 405 Method Not Allowed, 500 Internal Server Error
//
// Reference: RFC 9110 - https://httpwg.org/specs/rfc9110.html
// =============================================================================

#pragma once

#include <string>
#include <cstdint>

namespace aevrix {
namespace http {

// =============================================================================
// HTTP Status Codes Enumeration
// =============================================================================
// Defines the standard HTTP status codes as per RFC 9110.
// Using an enum class provides type safety and prevents invalid status codes.
// =============================================================================
enum class StatusCode : uint16_t {
    // =========================================================================
    // 2xx Success - The action was successfully received, understood, and accepted
    // =========================================================================
    
    /**
     * 200 OK
     * The request succeeded. The result meaning of "success" depends on the HTTP method:
     * - GET: The resource has been fetched and is transmitted in the message body.
     * - HEAD: The representation headers are included in the response without message body.
     * - PUT/POST: The resource describing the result of the action is transmitted.
     */
    OK = 200,

    /**
     * 201 Created
     * The request succeeded and a new resource was created.
     * Commonly used after PUT/POST requests.
     */
    Created = 201,

    /**
     * 204 No Content
     * The server successfully processed the request but is not returning any content.
     * Commonly used for DELETE requests or when updating without returning data.
     */
    NoContent = 204,

    // =========================================================================
    // 3xx Redirection - Further action needed to complete the request
    // =========================================================================
    
    /**
     * 301 Moved Permanently
     * The URL of the requested resource has been changed permanently.
     * The new URL is given in the Location header.
     */
    MovedPermanently = 301,

    /**
     * 302 Found
     * The URL of the requested resource has been changed temporarily.
     * The new URL is given in the Location header.
     */
    Found = 302,

    /**
     * 304 Not Modified
     * Indicates that the resource has not been modified since the version specified
     * by the request headers If-Modified-Since or If-None-Match.
     */
    NotModified = 304,

    // =========================================================================
    // 4xx Client Error - The request contains bad syntax or cannot be fulfilled
    // =========================================================================
    
    /**
     * 400 Bad Request
     * The server cannot or will not process the request due to something that is
     * perceived to be a client error (e.g., malformed request syntax, invalid
     * request message framing, or deceptive request routing).
     */
    BadRequest = 400,

    /**
     * 401 Unauthorized
     * The request has not been applied because it lacks valid authentication
     * credentials for the target resource.
     */
    Unauthorized = 401,

    /**
     * 403 Forbidden
     * The server understood the request but refuses to authorize it.
     * Unlike 401, authenticating will make no difference.
     */
    Forbidden = 403,

    /**
     * 404 Not Found
     * The origin server did not find a current representation for the target
     * resource or is not willing to disclose that one exists.
     */
    NotFound = 404,

    /**
     * 405 Method Not Allowed
     * The method received in the request-line is known by the origin server but
     * not supported by the target resource.
     */
    MethodNotAllowed = 405,

    /**
     * 409 Conflict
     * The request could not be completed due to a conflict with the current
     // state of the target resource.
     */
    Conflict = 409,

    /**
     * 413 Content Too Large
     * The server is refusing to process a request because the request payload
     * is larger than the server is willing or able to process.
     */
    ContentTooLarge = 413,

    /**
     * 429 Too Many Requests
     * The user has sent too many requests in a given amount of time ("rate limiting").
     */
    TooManyRequests = 429,

    // =========================================================================
    // 5xx Server Error - The server failed to fulfill a valid request
    // =========================================================================
    
    /**
     * 500 Internal Server Error
     * The server encountered an unexpected condition that prevented it from
     // fulfilling the request.
     */
    InternalServerError = 500,

    /**
     * 501 Not Implemented
     * The server does not support the functionality required to fulfill the request.
     */
    NotImplemented = 501,

    /**
     * 502 Bad Gateway
     * The server, while acting as a gateway or proxy, received an invalid response
     * from an inbound server it accessed while attempting to fulfill the request.
     */
    BadGateway = 502,

    /**
     * 503 Service Unavailable
     * The server is currently unable to handle the request due to a temporary
     // overload or scheduled maintenance.
     */
    ServiceUnavailable = 503,

    /**
     * 504 Gateway Timeout
     * The server, while acting as a gateway or proxy, did not receive a timely
     * response from an upstream server it needed to access in order to complete the request.
     */
    GatewayTimeout = 504
};

// =============================================================================
// Status Code Utilities
// =============================================================================

/**
 * @brief Convert status code to its numeric value
 * 
 * @param status The status code enum
 * @return uint16_t The numeric status code (e.g., 200 for OK)
 */
inline uint16_t status_code_to_int(StatusCode status) {
    return static_cast<uint16_t>(status);
}

/**
 * @brief Get the reason phrase for a status code
 * 
 * Returns the human-readable reason phrase that follows the status code
 * in an HTTP status line (e.g., "OK" for 200, "Not Found" for 404).
 * 
 * @param status The status code enum
 * @return std::string The reason phrase
 */
inline std::string status_code_to_string(StatusCode status) {
    switch (status) {
        // 2xx Success
        case StatusCode::OK: return "OK";
        case StatusCode::Created: return "Created";
        case StatusCode::NoContent: return "No Content";
        
        // 3xx Redirection
        case StatusCode::MovedPermanently: return "Moved Permanently";
        case StatusCode::Found: return "Found";
        case StatusCode::NotModified: return "Not Modified";
        
        // 4xx Client Error
        case StatusCode::BadRequest: return "Bad Request";
        case StatusCode::Unauthorized: return "Unauthorized";
        case StatusCode::Forbidden: return "Forbidden";
        case StatusCode::NotFound: return "Not Found";
        case StatusCode::MethodNotAllowed: return "Method Not Allowed";
        case StatusCode::Conflict: return "Conflict";
        case StatusCode::ContentTooLarge: return "Content Too Large";
        case StatusCode::TooManyRequests: return "Too Many Requests";
        
        // 5xx Server Error
        case StatusCode::InternalServerError: return "Internal Server Error";
        case StatusCode::NotImplemented: return "Not Implemented";
        case StatusCode::BadGateway: return "Bad Gateway";
        case StatusCode::ServiceUnavailable: return "Service Unavailable";
        case StatusCode::GatewayTimeout: return "Gateway Timeout";
        
        default: return "Unknown";
    }
}

/**
 * @brief Check if a status code indicates success (2xx)
 * 
 * @param status The status code enum
 * @return true if the status code is in the 2xx range
 */
inline bool is_success_status(StatusCode status) {
    uint16_t code = status_code_to_int(status);
    return code >= 200 && code < 300;
}

/**
 * @brief Check if a status code indicates a client error (4xx)
 * 
 * @param status The status code enum
 * @return true if the status code is in the 4xx range
 */
inline bool is_client_error(StatusCode status) {
    uint16_t code = status_code_to_int(status);
    return code >= 400 && code < 500;
}

/**
 * @brief Check if a status code indicates a server error (5xx)
 * 
 * @param status The status code enum
 * @return true if the status code is in the 5xx range
 */
inline bool is_server_error(StatusCode status) {
    uint16_t code = status_code_to_int(status);
    return code >= 500 && code < 600;
}

} // namespace http
} // namespace aevrix
