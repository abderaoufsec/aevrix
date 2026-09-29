// =============================================================================
// Aevrix - TLS Context Implementation (Phase 21)
// =============================================================================

#ifdef AEVRIX_ENABLE_TLS

#include "aevrix/tls_context.h"
#include "aevrix/logger.h"
#include <stdexcept>

namespace aevrix {

TlsContext::TlsContext() : ctx_(nullptr) {
    // Create SSL_CTX with TLS_server_method() for server-side TLS
    // This is the modern OpenSSL 1.1.0+ API
    ctx_ = SSL_CTX_new(TLS_server_method());

    if (!ctx_) {
        unsigned long err = ERR_get_error();
        char err_msg[256];
        ERR_error_string_n(err, err_msg, sizeof(err_msg));
        g_logger.error("Failed to create SSL_CTX: " + std::string(err_msg));
        throw std::runtime_error("Failed to create SSL_CTX: " + std::string(err_msg));
    }

    // Set secure options
    // SSL_OP_NO_SSLv2, SSL_OP_NO_SSLv3, SSL_OP_NO_TLSv1, SSL_OP_NO_TLSv1_1
    // Disable insecure protocols
    SSL_CTX_set_options(ctx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);

    // Enable strict server mode
    SSL_CTX_set_options(ctx_, SSL_OP_NO_COMPRESSION);

    g_logger.info("TLS context created successfully");
}

TlsContext::~TlsContext() {
    if (ctx_) {
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
        g_logger.info("TLS context destroyed");
    }
}

TlsContext::TlsContext(TlsContext&& other) noexcept
    : ctx_(other.ctx_) {
    other.ctx_ = nullptr;
}

TlsContext& TlsContext::operator=(TlsContext&& other) noexcept {
    if (this != &other) {
        if (ctx_) {
            SSL_CTX_free(ctx_);
        }
        ctx_ = other.ctx_;
        other.ctx_ = nullptr;
    }
    return *this;
}

void TlsContext::load_certificate_and_key(const std::string& cert_file, const std::string& key_file) {
    if (!ctx_) {
        throw std::runtime_error("SSL_CTX is null");
    }

    g_logger.info("Loading TLS certificate from: " + cert_file);

    // Load certificate
    if (SSL_CTX_use_certificate_file(ctx_, cert_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        unsigned long err = ERR_get_error();
        char err_msg[256];
        ERR_error_string_n(err, err_msg, sizeof(err_msg));
        g_logger.error("Failed to load certificate: " + std::string(err_msg));
        throw std::runtime_error("Failed to load certificate: " + std::string(err_msg));
    }

    g_logger.info("Loading TLS private key from: " + key_file);

    // Load private key
    if (SSL_CTX_use_PrivateKey_file(ctx_, key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        unsigned long err = ERR_get_error();
        char err_msg[256];
        ERR_error_string_n(err, err_msg, sizeof(err_msg));
        g_logger.error("Failed to load private key: " + std::string(err_msg));
        throw std::runtime_error("Failed to load private key: " + std::string(err_msg));
    }

    // Verify that certificate and private key match
    if (SSL_CTX_check_private_key(ctx_) != 1) {
        unsigned long err = ERR_get_error();
        char err_msg[256];
        ERR_error_string_n(err, err_msg, sizeof(err_msg));
        g_logger.error("Certificate and private key do not match: " + std::string(err_msg));
        throw std::runtime_error("Certificate and private key do not match: " + std::string(err_msg));
    }

    g_logger.info("Certificate and private key loaded and validated successfully");
}

void TlsContext::set_min_protocol_version(const std::string& min_version) {
    if (!ctx_) {
        throw std::runtime_error("SSL_CTX is null");
    }

    int version = version_string_to_constant(min_version);

    // SSL_CTX_set_min_proto_version expects a 16-bit version number
    // TLS1_2_VERSION is 0x0303, TLS1_3_VERSION is 0x0304
    if (SSL_CTX_set_min_proto_version(ctx_, static_cast<uint16_t>(version)) != 1) {
        g_logger.error("Failed to set minimum TLS version: " + min_version);
        throw std::runtime_error("Failed to set minimum TLS version: " + min_version);
    }

    g_logger.info("Minimum TLS version set to: " + min_version);
}

void TlsContext::set_max_protocol_version(const std::string& max_version) {
    if (!ctx_) {
        throw std::runtime_error("SSL_CTX is null");
    }

    int version = version_string_to_constant(max_version);

    // SSL_CTX_set_max_proto_version expects a 16-bit version number
    if (SSL_CTX_set_max_proto_version(ctx_, static_cast<uint16_t>(version)) != 1) {
        g_logger.error("Failed to set maximum TLS version: " + max_version);
        throw std::runtime_error("Failed to set maximum TLS version: " + max_version);
    }

    g_logger.info("Maximum TLS version set to: " + max_version);
}

int TlsContext::version_string_to_constant(const std::string& version) {
    if (version == "TLSv1.2") {
        return TLS1_2_VERSION;
    } else if (version == "TLSv1.3") {
        return TLS1_3_VERSION;
    } else {
        g_logger.error("Unsupported TLS version: " + version);
        throw std::runtime_error("Unsupported TLS version: " + version);
    }
}

} // namespace aevrix

#endif // AEVRIX_ENABLE_TLS
