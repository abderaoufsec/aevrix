// =============================================================================
// Aevrix - Per-Connection TLS State Implementation (Phase 21)
// =============================================================================

#ifdef AEVRIX_ENABLE_TLS

#include "aevrix/tls_connection.h"
#include "aevrix/tls_context.h"
#include "aevrix/logger.h"
#include <stdexcept>

namespace aevrix {

TlsConnection::TlsConnection(TlsContext& ctx, int fd)
    : ssl_(nullptr)
    , handshake_state_(TlsHandshakeState::NotStarted)
    , fd_(fd) {

    // Create SSL object from the context
    ssl_ = SSL_new(ctx.get());

    if (!ssl_) {
        unsigned long err = ERR_get_error();
        char err_msg[256];
        ERR_error_string_n(err, err_msg, sizeof(err_msg));
        g_logger.error("Failed to create SSL object: " + std::string(err_msg));
        throw std::runtime_error("Failed to create SSL object: " + std::string(err_msg));
    }

    // Associate SSL with the socket file descriptor
    if (SSL_set_fd(ssl_, fd_) != 1) {
        unsigned long err = ERR_get_error();
        char err_msg[256];
        ERR_error_string_n(err, err_msg, sizeof(err_msg));
        g_logger.error("Failed to set SSL file descriptor: " + std::string(err_msg));
        SSL_free(ssl_);
        ssl_ = nullptr;
        throw std::runtime_error("Failed to set SSL file descriptor: " + std::string(err_msg));
    }

    g_logger.log_with_connection(aevrix::LogLevel::DEBUG, static_cast<uint64_t>(fd), "TLS connection created");
}

TlsConnection::~TlsConnection() {
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
        g_logger.log_with_connection(aevrix::LogLevel::DEBUG, static_cast<uint64_t>(fd_), "TLS connection destroyed");
    }
}

TlsConnection::TlsConnection(TlsConnection&& other) noexcept
    : ssl_(other.ssl_)
    , handshake_state_(other.handshake_state_)
    , fd_(other.fd_) {
    other.ssl_ = nullptr;
    other.handshake_state_ = TlsHandshakeState::NotStarted;
    other.fd_ = -1;
}

TlsConnection& TlsConnection::operator=(TlsConnection&& other) noexcept {
    if (this != &other) {
        if (ssl_) {
            SSL_free(ssl_);
        }
        ssl_ = other.ssl_;
        handshake_state_ = other.handshake_state_;
        fd_ = other.fd_;
        other.ssl_ = nullptr;
        other.handshake_state_ = TlsHandshakeState::NotStarted;
        other.fd_ = -1;
    }
    return *this;
}

TlsIoRequirement TlsConnection::do_handshake() {
    if (!ssl_) {
        g_logger.log_with_connection(aevrix::LogLevel::ERR, static_cast<uint64_t>(fd_), "SSL is null during handshake");
        handshake_state_ = TlsHandshakeState::Failed;
        return TlsIoRequirement::Closed;
    }

    g_logger.log_with_connection(aevrix::LogLevel::DEBUG, static_cast<uint64_t>(fd_), "Starting TLS handshake");

    handshake_state_ = TlsHandshakeState::InProgress;

    // Perform SSL_accept() - nonblocking
    int result = SSL_accept(ssl_);

    if (result > 0) {
        // Handshake complete
        handshake_state_ = TlsHandshakeState::Complete;
        g_logger.log_with_connection(aevrix::LogLevel::INFO, static_cast<uint64_t>(fd_), "TLS handshake completed successfully");
        return TlsIoRequirement::None;
    }

    // Handle error
    int ssl_error = SSL_get_error(ssl_, result);
    TlsIoRequirement io_req = handle_ssl_error(ssl_error);

    if (io_req == TlsIoRequirement::Closed) {
        handshake_state_ = TlsHandshakeState::Failed;
        g_logger.log_with_connection(aevrix::LogLevel::ERR, static_cast<uint64_t>(fd_), "TLS handshake failed");
    }

    return io_req;
}

TlsIoRequirement TlsConnection::read(void* buffer, size_t size, size_t& bytes_read) {
    if (!ssl_) {
        g_logger.log_with_connection(aevrix::LogLevel::ERR, static_cast<uint64_t>(fd_), "SSL is null during read");
        bytes_read = 0;
        return TlsIoRequirement::Closed;
    }

    // Perform SSL_read() - nonblocking
    int result = SSL_read(ssl_, buffer, static_cast<int>(size));

    if (result > 0) {
        // Read successful
        bytes_read = static_cast<size_t>(result);
        return TlsIoRequirement::None;
    }

    // Handle error
    int ssl_error = SSL_get_error(ssl_, result);
    bytes_read = 0;

    if (ssl_error == SSL_ERROR_ZERO_RETURN) {
        // Peer closed connection cleanly
        g_logger.log_with_connection(aevrix::LogLevel::INFO, static_cast<uint64_t>(fd_), "TLS peer closed connection");
        return TlsIoRequirement::Closed;
    }

    return handle_ssl_error(ssl_error);
}

TlsIoRequirement TlsConnection::write(const void* buffer, size_t size, size_t& bytes_written) {
    if (!ssl_) {
        g_logger.log_with_connection(aevrix::LogLevel::ERR, static_cast<uint64_t>(fd_), "SSL is null during write");
        bytes_written = 0;
        return TlsIoRequirement::Closed;
    }

    // Perform SSL_write() - nonblocking
    int result = SSL_write(ssl_, buffer, static_cast<int>(size));

    if (result > 0) {
        // Write successful
        bytes_written = static_cast<size_t>(result);
        return TlsIoRequirement::None;
    }

    // Handle error
    int ssl_error = SSL_get_error(ssl_, result);
    bytes_written = 0;

    return handle_ssl_error(ssl_error);
}

TlsIoRequirement TlsConnection::shutdown() {
    if (!ssl_) {
        g_logger.log_with_connection(aevrix::LogLevel::ERR, static_cast<uint64_t>(fd_), "SSL is null during shutdown");
        return TlsIoRequirement::Closed;
    }

    g_logger.log_with_connection(aevrix::LogLevel::DEBUG, static_cast<uint64_t>(fd_), "Initiating TLS shutdown");

    // Perform SSL_shutdown() - nonblocking
    int result = SSL_shutdown(ssl_);

    if (result == 1) {
        // Shutdown complete (both directions)
        g_logger.log_with_connection(aevrix::LogLevel::INFO, static_cast<uint64_t>(fd_), "TLS shutdown completed");
        return TlsIoRequirement::Closed;
    }

    if (result == 0) {
        // Shutdown in progress (need to call again)
        int ssl_error = SSL_get_error(ssl_, result);
        return handle_ssl_error(ssl_error);
    }

    // Handle error
    int ssl_error = SSL_get_error(ssl_, result);
    return handle_ssl_error(ssl_error);
}

TlsIoRequirement TlsConnection::handle_ssl_error(int ssl_error) {
    switch (ssl_error) {
        case SSL_ERROR_WANT_READ:
            g_logger.log_with_connection(aevrix::LogLevel::DEBUG, static_cast<uint64_t>(fd_), "TLS needs read");
            return TlsIoRequirement::WantRead;

        case SSL_ERROR_WANT_WRITE:
            g_logger.log_with_connection(aevrix::LogLevel::DEBUG, static_cast<uint64_t>(fd_), "TLS needs write");
            return TlsIoRequirement::WantWrite;

        case SSL_ERROR_ZERO_RETURN:
            g_logger.log_with_connection(aevrix::LogLevel::INFO, static_cast<uint64_t>(fd_), "TLS connection closed by peer");
            return TlsIoRequirement::Closed;

        case SSL_ERROR_SYSCALL:
            // I/O error
            g_logger.log_with_connection(aevrix::LogLevel::ERR, static_cast<uint64_t>(fd_), "TLS I/O error");
            return TlsIoRequirement::Closed;

        case SSL_ERROR_SSL: {
            // TLS protocol error
            unsigned long err = ERR_get_error();
            char err_msg[256];
            ERR_error_string_n(err, err_msg, sizeof(err_msg));
            g_logger.log_with_connection(aevrix::LogLevel::ERR, static_cast<uint64_t>(fd_), "TLS protocol error: " + std::string(err_msg));
            return TlsIoRequirement::Closed;
        }

        default:
            g_logger.log_with_connection(aevrix::LogLevel::ERR, static_cast<uint64_t>(fd_), "Unknown TLS error: " + std::to_string(ssl_error));
            return TlsIoRequirement::Closed;
    }
}

} // namespace aevrix

#endif // AEVRIX_ENABLE_TLS
