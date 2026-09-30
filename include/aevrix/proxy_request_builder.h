// =============================================================================
// Aevrix - Proxy Request Builder
// =============================================================================
// Phase 22: builds the HTTP/1.1 request that Aevrix sends to an upstream on
// behalf of a client.
//
// Responsibilities:
// - path rewriting (optionally strip the routing prefix)
// - Host rewriting to the upstream authority
// - removal of hop-by-hop headers (RFC 9110 section 7.6.1)
// - regeneration of framing headers (Content-Length)
// - standard forwarding headers (X-Forwarded-For / -Proto / -Host)
//
// This is a pure transformation: no sockets, no I/O, no global state. That
// keeps the security-relevant logic (header filtering, injection prevention)
// under direct unit test.
//
// Why regenerate framing instead of forwarding it?
// The request body is delivered to the upstream as a single byte string, so
// the only correct framing is an exact Content-Length. Forwarding an inbound
// Transfer-Encoding (and any chunked body) would require re-encoding and is
// rejected upstream of this function.
// =============================================================================

#pragma once

#include <string>

#include "aevrix/http_request.h"
#include "aevrix/proxy_target.h"

namespace aevrix {

/**
 * @brief Options controlling how a client request is forwarded
 */
struct ProxyRequestOptions {
    std::string prefix;            // Path prefix that routes to the proxy
    bool strip_prefix = true;      // Remove the prefix before forwarding
    std::string forwarded_proto;   // Client-facing protocol ("http" / "https")
    std::string client_ip;         // Client peer address for X-Forwarded-For
};

/**
 * @brief Check whether a request target is handled by the proxy
 *
 * Matching is prefix-based on the path component only, so the query string
 * never influences routing. A prefix of "/proxy" matches "/proxy" and
 * "/proxy/x" but not "/proxyfoo".
 *
 * @param client_target The request target (path plus optional query)
 * @param prefix The configured proxy prefix
 * @return true if the request should be forwarded upstream
 */
bool proxy_target_matches(const std::string& client_target, const std::string& prefix);

/**
 * @brief Compute the upstream request target for a client request
 *
 * When strip_prefix is true the routing prefix is removed ("/api/users" with
 * prefix "/api" becomes "/users"). The query string is preserved.
 *
 * @param client_target The client request target
 * @param prefix The configured proxy prefix
 * @param strip_prefix Whether to remove the prefix
 * @return std::string The upstream request target (always starts with '/')
 */
std::string proxy_upstream_path(const std::string& client_target,
                                const std::string& prefix,
                                bool strip_prefix);

/**
 * @brief Check whether a (normalized) header name is hop-by-hop
 *
 * @param normalized_name Lowercase header name
 * @return true if the header must not be forwarded to the upstream
 */
bool is_hop_by_hop_header(const std::string& normalized_name);

/**
 * @brief Serialize the client request as an HTTP/1.1 request for the upstream
 *
 * @param request The parsed client request
 * @param target The upstream target
 * @param options Rewriting options
 * @return std::string The complete upstream request (head plus body)
 */
std::string build_upstream_request(const http::HttpRequest& request,
                                   const ProxyTarget& target,
                                   const ProxyRequestOptions& options);

} // namespace aevrix
