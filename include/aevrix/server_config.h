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
};

} // namespace aevrix
