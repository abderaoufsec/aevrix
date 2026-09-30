// =============================================================================
// Aevrix - Server Configuration
// =============================================================================
// This file implements the server configuration for timeouts and resource limits.
// In Phase 13, we add configuration file support to move runtime policy out
// of hard-coded constants.
//
// The configuration includes:
// - Host: Server binding address
// - Port: Server binding port
// - Workers: Number of worker threads
// - Document root: Static file serving directory
// - Header timeout: Maximum time to receive HTTP headers
// - Body timeout: Maximum time to receive HTTP body
// - Keep-alive timeout: Maximum idle time between requests
// - Write timeout: Maximum time to send response
// - Max connections: Maximum concurrent connections
// - Max buffer size: Maximum size for input/output buffers
// - Max request body: Maximum size for HTTP request body
//
// Previous Phases:
// - Phase 12: Router for application-level routing
//
// Future Phases Will Add:
// - Phase 14: Structured logging
// =============================================================================

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// Forward declaration for ConfigParser
namespace aevrix {
class ConfigParser;
}

namespace aevrix {

/**
 * @brief Server configuration for timeouts and resource limits
 * 
 * This class encapsulates all server-wide configuration parameters,
// particularly focusing on timeouts and resource limits to prevent
// slow-client resource exhaustion.
 * 
 * Timeouts prevent stalled connections from consuming resources indefinitely.
// Resource limits prevent malicious or buggy clients from exhausting memory
// or connection slots.
 * 
 * All timeouts are measured in milliseconds. All size limits are in bytes.
 */
class ServerConfig {
public:
    /**
     * @brief Constructor with default values
     * 
     * Creates a configuration with sensible defaults:
     * - Header timeout: 10 seconds
     * - Body timeout: 30 seconds
     * - Keep-alive timeout: 5 seconds
     * - Write timeout: 30 seconds
     * - Max connections: 1000
     * - Max buffer size: 64 KB
     * - Max request body: 10 MB
     */
    ServerConfig();

    /**
     * @brief Constructor with custom values
     * 
     * @param header_timeout_ms Timeout for receiving headers (ms)
     * @param body_timeout_ms Timeout for receiving body (ms)
     * @param keep_alive_timeout_ms Timeout for keep-alive idle (ms)
     * @param write_timeout_ms Timeout for sending response (ms)
     * @param max_connections Maximum concurrent connections
     * @param max_buffer_size Maximum buffer size (bytes)
     * @param max_request_body Maximum request body size (bytes)
     */
    ServerConfig(
        uint64_t header_timeout_ms,
        uint64_t body_timeout_ms,
        uint64_t keep_alive_timeout_ms,
        uint64_t write_timeout_ms,
        uint32_t max_connections,
        uint32_t max_buffer_size,
        uint32_t max_request_body
    );

    // =========================================================================
    // Timeout Configuration
    // =========================================================================

    /**
     * @brief Get the header timeout in milliseconds
     * 
     * Maximum time allowed to receive HTTP headers before closing the connection.
     * Prevents slow clients from holding connections open indefinitely.
     * 
     * @return uint64_t Header timeout in milliseconds
     */
    uint64_t header_timeout_ms() const { return header_timeout_ms_; }

    /**
     * @brief Set the header timeout in milliseconds
     * 
     * @param timeout_ms Header timeout in milliseconds
     */
    void set_header_timeout_ms(uint64_t timeout_ms) { header_timeout_ms_ = timeout_ms; }

    /**
     * @brief Get the body timeout in milliseconds
     * 
     * Maximum time allowed to receive HTTP body before closing the connection.
     * Prevents slow clients from holding connections open indefinitely during upload.
     * 
     * @return uint64_t Body timeout in milliseconds
     */
    uint64_t body_timeout_ms() const { return body_timeout_ms_; }

    /**
     * @brief Set the body timeout in milliseconds
     * 
     * @param timeout_ms Body timeout in milliseconds
     */
    void set_body_timeout_ms(uint64_t timeout_ms) { body_timeout_ms_ = timeout_ms; }

    /**
     * @brief Get the keep-alive timeout in milliseconds
     * 
     * Maximum idle time allowed between requests on a keep-alive connection.
     * Prevents idle connections from consuming connection slots indefinitely.
     * 
     * @return uint64_t Keep-alive timeout in milliseconds
     */
    uint64_t keep_alive_timeout_ms() const { return keep_alive_timeout_ms_; }

    /**
     * @brief Set the keep-alive timeout in milliseconds
     * 
     * @param timeout_ms Keep-alive timeout in milliseconds
     */
    void set_keep_alive_timeout_ms(uint64_t timeout_ms) { keep_alive_timeout_ms_ = timeout_ms; }

    /**
     * @brief Get the write timeout in milliseconds
     * 
     * Maximum time allowed to send the complete response before closing the connection.
     * Prevents slow readers from holding connections open indefinitely.
     * 
     * @return uint64_t Write timeout in milliseconds
     */
    uint64_t write_timeout_ms() const { return write_timeout_ms_; }

    /**
     * @brief Set the write timeout in milliseconds
     * 
     * @param timeout_ms Write timeout in milliseconds
     */
    void set_write_timeout_ms(uint64_t timeout_ms) { write_timeout_ms_ = timeout_ms; }

    // =========================================================================
    // Resource Limit Configuration
    // =========================================================================

    /**
     * @brief Get the maximum number of concurrent connections
     * 
     * Prevents connection exhaustion by limiting total concurrent connections.
     * When this limit is reached, new connections are rejected with 503 Service Unavailable.
     * 
     * @return uint32_t Maximum concurrent connections
     */
    uint32_t max_connections() const { return max_connections_; }

    /**
     * @brief Set the maximum number of concurrent connections
     * 
     * @param max_conn Maximum concurrent connections
     */
    void set_max_connections(uint32_t max_conn) { max_connections_ = max_conn; }

    /**
     * @brief Get the maximum buffer size in bytes
     * 
     * Maximum size for input and output buffers. Prevents memory exhaustion
     * from malicious clients sending or requesting large amounts of data.
     * 
     * @return uint32_t Maximum buffer size in bytes
     */
    uint32_t max_buffer_size() const { return max_buffer_size_; }

    /**
     * @brief Set the maximum buffer size in bytes
     * 
     * @param size Maximum buffer size in bytes
     */
    void set_max_buffer_size(uint32_t size) { max_buffer_size_ = size; }

    /**
     * @brief Get the maximum request body size in bytes
     * 
     * Maximum size for HTTP request body. Prevents memory exhaustion
     // from malicious clients uploading large bodies.
     * 
     * @return uint32_t Maximum request body size in bytes
     */
    uint32_t max_request_body() const { return max_request_body_; }

    /**
     * @brief Set the maximum request body size in bytes
     * 
     * @param size Maximum request body size in bytes
     */
    void set_max_request_body(uint32_t size) { max_request_body_ = size; }

    // =========================================================================
    // Validation Methods
    // =========================================================================

    /**
     * @brief Check if a buffer size exceeds the limit
     * 
     * @param size Buffer size in bytes
     * @return true if size is within limits, false if exceeds limit
     */
    bool is_buffer_size_valid(uint32_t size) const {
        return size <= max_buffer_size_;
    }

    /**
     * @brief Check if a request body size exceeds the limit
     * 
     * @param size Request body size in bytes
     * @return true if size is within limits, false if exceeds limit
     */
    bool is_request_body_valid(uint32_t size) const {
        return size <= max_request_body_;
    }

    /**
     * @brief Check if a connection count exceeds the limit
     * 
     * @param count Current connection count
     * @return true if count is within limits, false if exceeds limit
     */
    bool is_connection_count_valid(uint32_t count) const {
        return count <= max_connections_;
    }

    /**
     * @brief Get a human-readable configuration summary
     * 
     * @return std::string Configuration summary
     */
    std::string summary() const;

    // =========================================================================
    // Configuration Loading (Phase 13)
    // =========================================================================

    /**
     * @brief Load configuration from ConfigParser
     * 
     * @param parser The configuration parser
     * @throws std::runtime_error if configuration is invalid
     */
    void load_from_parser(const ConfigParser& parser);

    /**
     * @brief Get the host address
     * 
     * @return std::string The host address
     */
    const std::string& host() const { return host_; }

    /**
     * @brief Set the host address
     * 
     * @param host The host address
     */
    void set_host(const std::string& host) { host_ = host; }

    /**
     * @brief Get the port number
     * 
     * @return uint16_t The port number
     */
    uint16_t port() const { return port_; }

    /**
     * @brief Set the port number
     * 
     * @param port The port number
     */
    void set_port(uint16_t port) { port_ = port; }

    /**
     * @brief Get the number of worker threads
     * 
     * @return uint32_t The number of workers
     */
    uint32_t workers() const { return workers_; }

    /**
     * @brief Set the number of worker threads
     * 
     * @param workers The number of workers
     */
    void set_workers(uint32_t workers) { workers_ = workers; }

    /**
     * @brief Get the document root directory
     * 
     * @return std::string The document root directory
     */
    const std::string& document_root() const { return document_root_; }

    /**
     * @brief Set the document root directory
     * 
     * @param document_root The document root directory
     */
    void set_document_root(const std::string& document_root) { document_root_ = document_root; }

    // =========================================================================
    // TLS Configuration (Phase 21)
    // =========================================================================

    /**
     * @brief Check if TLS is enabled
     * 
     * @return true if TLS is enabled, false otherwise
     */
    bool tls_enabled() const { return tls_enabled_; }

    /**
     * @brief Set whether TLS is enabled
     * 
     * @param enabled true to enable TLS, false to disable
     */
    void set_tls_enabled(bool enabled) { tls_enabled_ = enabled; }

    /**
     * @brief Get the TLS certificate file path
     * 
     * @return const std::string& The certificate file path
     */
    const std::string& tls_cert_file() const { return tls_cert_file_; }

    /**
     * @brief Set the TLS certificate file path
     * 
     * @param cert_file The certificate file path
     */
    void set_tls_cert_file(const std::string& cert_file) { tls_cert_file_ = cert_file; }

    /**
     * @brief Get the TLS private key file path
     * 
     * @return const std::string& The private key file path
     */
    const std::string& tls_key_file() const { return tls_key_file_; }

    /**
     * @brief Set the TLS private key file path
     * 
     * @param key_file The private key file path
     */
    void set_tls_key_file(const std::string& key_file) { tls_key_file_ = key_file; }

    /**
     * @brief Get the minimum TLS version
     * 
     * @return const std::string& The minimum TLS version (e.g., "TLSv1.2")
     */
    const std::string& tls_min_version() const { return tls_min_version_; }

    /**
     * @brief Set the minimum TLS version
     * 
     * @param min_version The minimum TLS version
     */
    void set_tls_min_version(const std::string& min_version) { tls_min_version_ = min_version; }

    /**
     * @brief Get the maximum TLS version
     * 
     * @return const std::string& The maximum TLS version (e.g., "TLSv1.3")
     */
    const std::string& tls_max_version() const { return tls_max_version_; }

    /**
     * @brief Set the maximum TLS version
     * 
     * @param max_version The maximum TLS version
     */
    void set_tls_max_version(const std::string& max_version) { tls_max_version_ = max_version; }

    /**
     * @brief Get the TLS listen port
     * 
     * @return uint16_t The TLS listen port
     */
    uint16_t tls_port() const { return tls_port_; }

    /**
     * @brief Set the TLS listen port
     * 
     * @param port The TLS listen port
     */
    void set_tls_port(uint16_t port) { tls_port_ = port; }

    // =========================================================================
    // Reverse Proxy Configuration (Phase 22)
    // =========================================================================

    /**
     * @brief Whether reverse-proxy routing is enabled
     */
    bool proxy_enabled() const { return proxy_enabled_; }

    /**
     * @brief Enable or disable reverse-proxy routing
     */
    void set_proxy_enabled(bool enabled) { proxy_enabled_ = enabled; }

    /**
     * @brief Get the upstream specification (e.g. "http://127.0.0.1:9001")
     */
    const std::string& proxy_pass() const { return proxy_pass_; }

    /**
     * @brief Set the upstream specification
     */
    void set_proxy_pass(const std::string& proxy_pass) { proxy_pass_ = proxy_pass; }

    /**
     * @brief Get the path prefix routed to the upstream (e.g. "/proxy")
     */
    const std::string& proxy_prefix() const { return proxy_prefix_; }

    /**
     * @brief Set the path prefix routed to the upstream
     */
    void set_proxy_prefix(const std::string& prefix) { proxy_prefix_ = prefix; }

    /**
     * @brief Whether the prefix is removed before forwarding
     */
    bool proxy_strip_prefix() const { return proxy_strip_prefix_; }

    /**
     * @brief Set whether the prefix is removed before forwarding
     */
    void set_proxy_strip_prefix(bool strip) { proxy_strip_prefix_ = strip; }

    /**
     * @brief Maximum time allowed for the upstream TCP connect (ms)
     */
    uint64_t proxy_connect_timeout_ms() const { return proxy_connect_timeout_ms_; }

    /**
     * @brief Set the upstream connect timeout (ms)
     */
    void set_proxy_connect_timeout_ms(uint64_t timeout_ms) { proxy_connect_timeout_ms_ = timeout_ms; }

    /**
     * @brief Maximum time allowed for the upstream response (ms)
     */
    uint64_t proxy_read_timeout_ms() const { return proxy_read_timeout_ms_; }

    /**
     * @brief Set the upstream read timeout (ms)
     */
    void set_proxy_read_timeout_ms(uint64_t timeout_ms) { proxy_read_timeout_ms_ = timeout_ms; }

    /**
     * @brief Maximum idle keep-alive connections kept per upstream
     */
    uint32_t proxy_max_idle_connections() const { return proxy_max_idle_connections_; }

    /**
     * @brief Set the maximum idle keep-alive connections per upstream
     */
    void set_proxy_max_idle_connections(uint32_t value) { proxy_max_idle_connections_ = value; }

    /**
     * @brief Maximum upstream response body size buffered (bytes)
     */
    uint32_t proxy_max_response_bytes() const { return proxy_max_response_bytes_; }

    /**
     * @brief Set the maximum upstream response body size (bytes)
     */
    void set_proxy_max_response_bytes(uint32_t value) { proxy_max_response_bytes_ = value; }

    /**
     * @brief Idle timeout for pooled upstream connections (ms)
     */
    uint64_t proxy_idle_timeout_ms() const { return proxy_idle_timeout_ms_; }

    /**
     * @brief Set the pooled connection idle timeout (ms)
     */
    void set_proxy_idle_timeout_ms(uint64_t timeout_ms) { proxy_idle_timeout_ms_ = timeout_ms; }

    // =========================================================================
    // WebSocket Configuration (Phase 23)
    // =========================================================================

    /**
     * @brief Whether the WebSocket upgrade endpoint is enabled
     */
    bool websocket_enabled() const { return websocket_enabled_; }

    /**
     * @brief Enable or disable WebSocket upgrades
     */
    void set_websocket_enabled(bool enabled) { websocket_enabled_ = enabled; }

    /**
     * @brief Paths that accept WebSocket upgrades (exact match, no query)
     */
    const std::vector<std::string>& websocket_allowed_paths() const { return websocket_allowed_paths_; }

    /**
     * @brief Replace the WebSocket path allowlist
     */
    void set_websocket_allowed_paths(std::vector<std::string> paths) { websocket_allowed_paths_ = std::move(paths); }

    /**
     * @brief Ceiling for a single WebSocket frame or assembled message (bytes)
     */
    uint32_t websocket_max_message_bytes() const { return websocket_max_message_bytes_; }

    /**
     * @brief Set the WebSocket message size ceiling (bytes)
     */
    void set_websocket_max_message_bytes(uint32_t bytes) { websocket_max_message_bytes_ = bytes; }

    /**
     * @brief Budget for completing a WebSocket close handshake (ms)
     */
    uint64_t websocket_close_timeout_ms() const { return websocket_close_timeout_ms_; }

    /**
     * @brief Set the WebSocket close handshake budget (ms)
     */
    void set_websocket_close_timeout_ms(uint64_t timeout_ms) { websocket_close_timeout_ms_ = timeout_ms; }

    /**
     * @brief Server-initiated Ping interval (ms); 0 disables keepalive pings
     */
    uint64_t websocket_ping_interval_ms() const { return websocket_ping_interval_ms_; }

    /**
     * @brief Set the server-initiated Ping interval (ms, 0 = disabled)
     */
    void set_websocket_ping_interval_ms(uint64_t interval_ms) { websocket_ping_interval_ms_ = interval_ms; }

    /**
     * @brief Allowed Origin values for WebSocket upgrades (empty = allow any)
     */
    const std::vector<std::string>& websocket_allowed_origins() const { return websocket_allowed_origins_; }

    /**
     * @brief Replace the WebSocket Origin allowlist (empty = allow any)
     */
    void set_websocket_allowed_origins(std::vector<std::string> origins) { websocket_allowed_origins_ = std::move(origins); }

private:
    // =========================================================================
    // Timeout Configuration (milliseconds)
    // =========================================================================

    uint64_t header_timeout_ms_;        // Timeout for receiving headers
    uint64_t body_timeout_ms_;          // Timeout for receiving body
    uint64_t keep_alive_timeout_ms_;    // Timeout for keep-alive idle
    uint64_t write_timeout_ms_;         // Timeout for sending response

    // =========================================================================
    // Resource Limit Configuration (bytes)
    // =========================================================================

    uint32_t max_connections_;         // Maximum concurrent connections
    uint32_t max_buffer_size_;         // Maximum buffer size
    uint32_t max_request_body_;        // Maximum request body size

    // =========================================================================
    // Server Configuration (Phase 13)
    // =========================================================================

    std::string host_;                // Server binding address
    uint16_t port_;                    // Server binding port
    uint32_t workers_;                 // Number of worker threads
    std::string document_root_;        // Static file serving directory

    // =========================================================================
    // TLS Configuration (Phase 21)
    // =========================================================================

    bool tls_enabled_ = false;         // Whether TLS is enabled
    std::string tls_cert_file_;        // TLS certificate file path
    std::string tls_key_file_;         // TLS private key file path
    std::string tls_min_version_;      // Minimum TLS version (e.g., "TLSv1.2")
    std::string tls_max_version_;      // Maximum TLS version (e.g., "TLSv1.3")
    uint16_t tls_port_ = 443;          // TLS listen port (default 443)

    // =========================================================================
    // Reverse Proxy Configuration (Phase 22)
    // =========================================================================

    bool proxy_enabled_ = false;                    // Whether proxying is enabled
    std::string proxy_pass_;                        // Upstream specification
    std::string proxy_prefix_ = "/proxy";           // Routed path prefix
    bool proxy_strip_prefix_ = true;                // Strip prefix when forwarding
    uint64_t proxy_connect_timeout_ms_ = 5000;      // Upstream connect budget
    uint64_t proxy_read_timeout_ms_ = 30000;        // Upstream response budget
    uint32_t proxy_max_idle_connections_ = 4;       // Idle connections per upstream
    uint32_t proxy_max_response_bytes_ = 4 * 1024 * 1024;  // Buffered body cap
    uint64_t proxy_idle_timeout_ms_ = 60000;        // Pooled connection idle budget

    // =========================================================================
    // WebSocket Configuration (Phase 23)
    // =========================================================================

    bool websocket_enabled_ = false;                        // Opt-in feature flag
    std::vector<std::string> websocket_allowed_paths_{"/ws"};  // Upgrade paths
    uint32_t websocket_max_message_bytes_ = 1024 * 1024;    // 1 MiB frame/message cap
    uint64_t websocket_close_timeout_ms_ = 5000;            // Close handshake budget
    uint64_t websocket_ping_interval_ms_ = 0;               // 0 disables server pings
    std::vector<std::string> websocket_allowed_origins_;    // Empty = allow any
};

} // namespace aevrix
