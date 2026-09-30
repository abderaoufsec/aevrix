// =============================================================================
// Aevrix - Reverse Proxy Upstream Target
// =============================================================================
// Phase 22 introduces reverse-proxy support:
//
//   client → Aevrix → upstream
//
// An upstream target is the (host, port) pair that proxied requests are
// forwarded to. This component is deliberately free of sockets and I/O: it
// only turns a configuration string such as "http://127.0.0.1:9001" into a
// validated ProxyTarget.
//
// Why keep this pure?
// - Configuration parsing errors must be caught at startup, not inside the
//   event loop (rule: configuration never performs I/O).
// - Pure functions are directly unit testable without network access.
//
// Previous Phases:
// - Phase 21: TLS transport layer
// =============================================================================

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace aevrix {

// =============================================================================
// ProxyTarget
// =============================================================================
// The validated upstream endpoint. "authority()" returns the "host:port" form
// used for the upstream Host header and as the connection-pool key.
// =============================================================================
struct ProxyTarget {
    std::string host;      // Hostname or literal address (no brackets for IPv6)
    uint16_t port = 0;     // TCP port (never 0 for a valid target)

    /**
     * @brief The "host:port" authority string
     *
     * Used both as the Host header value for upstream requests and as the
     * connection-pool key so that idle connections are only reused for the
     * upstream they were opened for.
     *
     * @return std::string The authority (e.g. "127.0.0.1:9001")
     */
    std::string authority() const;

    /**
     * @brief Equality comparison (used for pool keying and tests)
     */
    bool operator==(const ProxyTarget& other) const {
        return host == other.host && port == other.port;
    }

    /**
     * @brief Inequality comparison
     */
    bool operator!=(const ProxyTarget& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Parse an upstream target specification
 *
 * Accepted forms:
 * - http://host          (port defaults to 80)
 * - http://host:port
 * - host:port
 * - host                 (port defaults to 80)
 *
 * A path component in the value is ignored: Aevrix forwards the client path
 * (optionally with the configured prefix stripped) and never rewrites it into
 * an upstream base path.
 *
 * TLS upstreams are rejected: Aevrix terminates TLS for clients but does not
 * yet originate TLS towards upstreams. Rejecting "https://" loudly at startup
 * is preferable to silently forwarding plaintext to a TLS port.
 *
 * @param value The raw configuration value
 * @param error Receives a human-readable reason when parsing fails
 * @return std::optional<ProxyTarget> The parsed target, or nullopt on failure
 */
std::optional<ProxyTarget> parse_proxy_target(const std::string& value, std::string& error);

} // namespace aevrix
