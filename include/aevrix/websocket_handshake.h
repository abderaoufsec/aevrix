// =============================================================================
// Aevrix - WebSocket HTTP Upgrade Handshake (RFC 6455 Section 4)
// =============================================================================
// This file validates an HTTP request as a WebSocket upgrade candidate and
// produces the 101 Switching Protocols response:
//
//   Sec-WebSocket-Accept = base64( SHA-1( key + GUID ) )
//   GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
//
// Design notes:
// - Validation reads the already-parsed HttpRequest; the HTTP request parser
//   itself is NOT modified (Phase 23 constraint).
// - SHA-1 and base64 are implemented locally so the handshake works in
//   TLS-OFF builds without linking OpenSSL.
// - Requests that merely look at a WebSocket path without upgrade intent are
//   reported as NotAnUpgrade so normal HTTP handling continues (404, etc.).
// - Rejections map to 400 / 403 / 426 responses built with HttpResponse.
// =============================================================================

#pragma once

#include <string>

#include "aevrix/http_request.h"
#include "aevrix/http_response.h"

namespace aevrix {
class ServerConfig;
}

namespace aevrix {
namespace ws {

/// RFC 6455 magic GUID appended to the client key before SHA-1.
extern const char* const kWebSocketGuid;

// =============================================================================
// Upgrade Evaluation
// =============================================================================

/**
 * @brief Outcome of evaluating a request as a WebSocket upgrade
 */
enum class UpgradeVerdict {
    NotAnUpgrade,        // No (or non-WebSocket) upgrade intent: normal HTTP
    Accepted,            // Valid upgrade: send the 101 response
    BadRequest,          // Upgrade intent but malformed (400)
    ForbiddenOrigin,     // Origin not in the allowlist (403)
    UnsupportedVersion   // Sec-WebSocket-Version != 13 (426)
};

/**
 * @brief Result of upgrade evaluation
 */
struct UpgradeResult {
    UpgradeVerdict verdict = UpgradeVerdict::NotAnUpgrade;
    std::string accept;     // Sec-WebSocket-Accept value (Accepted only)
    std::string response;   // Raw 101 response bytes (Accepted only)
};

/**
 * @brief Check whether the request attempts a WebSocket upgrade
 *
 * True when the Upgrade header contains the "websocket" token
 * (case-insensitive, RFC 9110 token comparison).
 */
bool is_websocket_intent(const http::HttpRequest& request);

/**
 * @brief Compute Sec-WebSocket-Accept for a client key
 *
 * @param client_key Base64 Sec-WebSocket-Key from the request
 * @return base64(SHA-1(key + GUID)); empty if the key is not valid
 *         base64 decoding to exactly 16 bytes (RFC 6455 Section 4.2.2)
 */
std::string compute_accept(const std::string& client_key);

/**
 * @brief Build the raw 101 Switching Protocols response
 */
std::string build_switching_protocols(const std::string& accept);

/**
 * @brief Evaluate a request against the WebSocket configuration
 *
 * Checks, in order: enabled flag, path allowlist, upgrade intent, method and
 * HTTP version, Connection token, Sec-WebSocket-Version, Sec-WebSocket-Key,
 * and Origin allowlist.
 *
 * @param request The parsed HTTP request
 * @param config  Server configuration (websocket_* settings)
 * @return UpgradeResult with verdict and (on acceptance) response bytes
 */
UpgradeResult evaluate_upgrade(const http::HttpRequest& request,
                               const ServerConfig& config);

/**
 * @brief Build the HTTP error response for a rejection verdict
 *
 * 400 for malformed upgrades, 403 for disallowed origins, 426 (with
 * Sec-WebSocket-Version: 13) for unsupported versions. All responses use
 * Connection: close.
 */
http::HttpResponse build_rejection_response(UpgradeVerdict verdict);

} // namespace ws
} // namespace aevrix
