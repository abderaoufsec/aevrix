// =============================================================================
// Aevrix - TLS Context Management (Phase 21)
// =============================================================================
// This file provides RAII management for OpenSSL SSL_CTX objects.
// The TLS context owns the server's certificate, private key, and protocol
// configuration. Each connection uses SSL objects created from this context.
//
// Key Features:
// - RAII-based SSL_CTX ownership
// - Secure default configuration
// - Certificate and private key loading
// - Protocol version configuration
// - Certificate/key validation
// - No raw ownership exposed
//
// Architecture:
// SSL_CTX (owned by TlsContext)
//     ↓
// SSL (per-connection, owned by TlsConnection)
//     ↓
// Socket FD
// =============================================================================

#pragma once

#ifdef AEVRIX_ENABLE_TLS

#include <string>
#include <memory>
#include <stdexcept>

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace aevrix {

/**
 * @brief RAII wrapper for OpenSSL SSL_CTX
 *
 * Manages the lifecycle of an OpenSSL SSL_CTX object, which contains
 * the server's certificate, private key, and protocol configuration.
 * Each connection creates SSL objects from this context.
 *
 * This class ensures:
 * - SSL_CTX is properly initialized with secure defaults
 * - Certificate and private key are loaded and validated
 * - SSL_CTX is properly cleaned up on destruction
 * - No raw ownership leaks to calling code
 */
class TlsContext {
public:
    /**
     * @brief Construct a TLS context
     *
     * Creates an SSL_CTX with TLS_server_method() for server-side TLS.
     *
     * @throws std::runtime_error if SSL_CTX creation fails
     */
    TlsContext();

    /**
     * @brief Destructor
     *
     * Automatically frees the SSL_CTX using SSL_CTX_free().
     */
    ~TlsContext();

    // Delete copy operations (SSL_CTX cannot be safely copied)
    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;

    // Allow move operations
    TlsContext(TlsContext&& other) noexcept;
    TlsContext& operator=(TlsContext&& other) noexcept;

    /**
     * @brief Load certificate and private key
     *
     * Loads the server certificate and private key from the specified files.
     * Validates that the certificate and key match.
     *
     * @param cert_file Path to the certificate file (PEM format)
     * @param key_file Path to the private key file (PEM format)
     * @throws std::runtime_error if loading or validation fails
     */
    void load_certificate_and_key(const std::string& cert_file, const std::string& key_file);

    /**
     * @brief Configure minimum TLS protocol version
     *
     * Sets the minimum allowed TLS protocol version. Invalid or insecure
     * versions are rejected.
     *
     * @param min_version Minimum TLS version (e.g., "TLSv1.2", "TLSv1.3")
     * @throws std::runtime_error if version is invalid or unsupported
     */
    void set_min_protocol_version(const std::string& min_version);

    /**
     * @brief Configure maximum TLS protocol version
     *
     * Sets the maximum allowed TLS protocol version.
     *
     * @param max_version Maximum TLS version (e.g., "TLSv1.2", "TLSv1.3")
     * @throws std::runtime_error if version is invalid or unsupported
     */
    void set_max_protocol_version(const std::string& max_version);

    /**
     * @brief Get the raw SSL_CTX pointer
     *
     * Returns the underlying SSL_CTX pointer for use with OpenSSL APIs.
     * Ownership remains with TlsContext.
     *
     * @return SSL_CTX* The SSL_CTX pointer
     */
    SSL_CTX* get() const { return ctx_; }

    /**
     * @brief Check if the context is valid
     *
     * @return true if the context is valid, false otherwise
     */
    bool is_valid() const { return ctx_ != nullptr; }

private:
    /**
     * @brief Convert version string to OpenSSL constant
     *
     * @param version Version string (e.g., "TLSv1.2", "TLSv1.3")
     * @return int OpenSSL protocol version constant
     * @throws std::runtime_error if version is invalid
     */
    static int version_string_to_constant(const std::string& version);

    SSL_CTX* ctx_;  // Owned SSL_CTX pointer
};

} // namespace aevrix

#endif // AEVRIX_ENABLE_TLS
