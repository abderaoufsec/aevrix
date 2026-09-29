// =============================================================================
// Aevrix - Per-Connection TLS State (Phase 21)
// =============================================================================
// This file provides RAII management for per-connection SSL objects.
// Each TLS-enabled connection has its own SSL object created from the
// server's TLS context.
//
// Key Features:
// - RAII-based SSL ownership
// - Nonblocking handshake state tracking
// - WANT_READ/WANT_WRITE tracking for epoll integration
// - Handshake completion detection
// - TLS shutdown handling
//
// Architecture:
// TlsContext (server-wide)
//     ↓
// TlsConnection (per-connection)
//     ↓
// Socket FD
// =============================================================================

#pragma once

#ifdef AEVRIX_ENABLE_TLS

#include <memory>
#include <cstdint>

#include <openssl/ssl.h>

namespace aevrix {

// Forward declaration
class TlsContext;

/**
 * @brief TLS I/O requirement for epoll integration
 *
 * Indicates what the connection needs from the event loop.
 * TLS can invert the normal read/write expectations:
 * - SSL_read() may return WANT_WRITE (needs EPOLLOUT)
 * - SSL_write() may return WANT_READ (needs EPOLLIN)
 */
enum class TlsIoRequirement {
    None,       // No I/O required
    WantRead,   // Connection needs EPOLLIN
    WantWrite,  // Connection needs EPOLLOUT
    Both,       // Connection needs both EPOLLIN and EPOLLOUT
    Closed      // Connection is closed
};

/**
 * @brief TLS handshake state
 */
enum class TlsHandshakeState {
    NotStarted,     // Handshake not started
    InProgress,     // Handshake in progress
    Complete,       // Handshake complete
    Failed          // Handshake failed
};

/**
 * @brief RAII wrapper for per-connection SSL object
 *
 * Manages the lifecycle of an SSL object for a single TLS connection.
 * Tracks handshake state and I/O requirements for epoll integration.
 *
 * This class ensures:
 * - SSL object is properly created from the context
 * - SSL object is properly cleaned up on destruction
 * - Handshake state is tracked explicitly
 * - I/O requirements are communicated to the event loop
 */
class TlsConnection {
public:
    /**
     * @brief Construct a TLS connection
     *
     * Creates an SSL object from the given TLS context and associates
     * it with the specified socket file descriptor.
     *
     * @param ctx The TLS context to create SSL from
     * @param fd The socket file descriptor
     * @throws std::runtime_error if SSL creation fails
     */
    TlsConnection(TlsContext& ctx, int fd);

    /**
     * @brief Destructor
     *
     * Automatically frees the SSL object using SSL_free().
     */
    ~TlsConnection();

    // Delete copy operations (SSL cannot be safely copied)
    TlsConnection(const TlsConnection&) = delete;
    TlsConnection& operator=(const TlsConnection&) = delete;

    // Allow move operations
    TlsConnection(TlsConnection&& other) noexcept;
    TlsConnection& operator=(TlsConnection&& other) noexcept;

    /**
     * @brief Perform nonblocking TLS handshake
     *
     * Calls SSL_accept() in nonblocking mode. Handles WANT_READ/WANT_WRITE
     * and updates handshake state and I/O requirements accordingly.
     *
     * @return TlsIoRequirement indicating what the event loop should wait for
     */
    TlsIoRequirement do_handshake();

    /**
     * @brief Read data through TLS
     *
     * Calls SSL_read() in nonblocking mode. Handles WANT_READ/WANT_WRITE
     * and updates I/O requirements accordingly.
     *
     * @param buffer Buffer to read into
     * @param size Buffer size
     * @param[out] bytes_read Number of bytes read
     * @return TlsIoRequirement indicating what the event loop should wait for
     */
    TlsIoRequirement read(void* buffer, size_t size, size_t& bytes_read);

    /**
     * @brief Write data through TLS
     *
     * Calls SSL_write() in nonblocking mode. Handles WANT_READ/WANT_WRITE
     * and updates I/O requirements accordingly.
     *
     * @param buffer Buffer to write from
     * @param size Buffer size
     * @param[out] bytes_written Number of bytes written
     * @return TlsIoRequirement indicating what the event loop should wait for
     */
    TlsIoRequirement write(const void* buffer, size_t size, size_t& bytes_written);

    /**
     * @brief Shutdown TLS connection
     *
     * Initiates TLS shutdown. Returns the I/O requirement for the shutdown
     * process (shutdown may need multiple steps in nonblocking mode).
     *
     * @return TlsIoRequirement indicating what the event loop should wait for
     */
    TlsIoRequirement shutdown();

    /**
     * @brief Get the current handshake state
     *
     * @return TlsHandshakeState The handshake state
     */
    TlsHandshakeState handshake_state() const { return handshake_state_; }

    /**
     * @brief Check if handshake is complete
     *
     * @return true if handshake is complete, false otherwise
     */
    bool is_handshake_complete() const { return handshake_state_ == TlsHandshakeState::Complete; }

    /**
     * @brief Check if handshake failed
     *
     * @return true if handshake failed, false otherwise
     */
    bool is_handshake_failed() const { return handshake_state_ == TlsHandshakeState::Failed; }

    /**
     * @brief Get the raw SSL pointer
     *
     * Returns the underlying SSL pointer for use with OpenSSL APIs.
     * Ownership remains with TlsConnection.
     *
     * @return SSL* The SSL pointer
     */
    SSL* get() const { return ssl_; }

    /**
     * @brief Check if the connection is valid
     *
     * @return true if the connection is valid, false otherwise
     */
    bool is_valid() const { return ssl_ != nullptr; }

private:
    /**
     * @brief Handle SSL error
     *
     * Converts SSL error codes to TlsIoRequirement.
     *
     * @param ssl_error The SSL error code from SSL_get_error()
     * @return TlsIoRequirement The corresponding I/O requirement
     */
    TlsIoRequirement handle_ssl_error(int ssl_error);

    SSL* ssl_;                      // Owned SSL pointer
    TlsHandshakeState handshake_state_;  // Handshake state
    int fd_;                        // Socket file descriptor
};

} // namespace aevrix

#endif // AEVRIX_ENABLE_TLS
