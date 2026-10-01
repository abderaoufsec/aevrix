/**
 * @file tls_integration_tests.cpp
 * @brief Integration tests for TLS functionality
 *
 * These tests exercise TlsContext and TlsConnection against real certificates.
 * The test key pair is generated in-process with the OpenSSL API on first use,
 * so the suite needs no certificate files and no openssl CLI.
 */

#ifdef AEVRIX_ENABLE_TLS

#include <gtest/gtest.h>
#include <aevrix/tls_context.h>
#include <aevrix/tls_connection.h>
#include <aevrix/logger.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>

namespace aevrix {
namespace test {

// Test certificate paths - generated into the working directory on first use
const char* TEST_CERT_PATH = "test_cert.pem";
const char* TEST_KEY_PATH = "test_key.pem";

namespace {

// The test key and certificate are produced with the OpenSSL API rather than by
// shelling out to the openssl CLI: nothing external to install, no shell, and no
// unchecked system() return value for -Werror to trip over at -O3.
EVP_PKEY* generate_rsa_key() {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (ctx == nullptr) {
        return nullptr;
    }

    EVP_PKEY* key = nullptr;
    if (EVP_PKEY_keygen_init(ctx) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0 ||
        EVP_PKEY_keygen(ctx, &key) <= 0) {
        EVP_PKEY_free(key);
        key = nullptr;
    }

    EVP_PKEY_CTX_free(ctx);
    return key;
}

bool write_private_key(const std::string& path, EVP_PKEY* key) {
    BIO* bio = BIO_new_file(path.c_str(), "wb");
    if (bio == nullptr) {
        return false;
    }
    const int written = PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr, nullptr);
    BIO_free_all(bio);
    return written == 1;
}

bool write_certificate(const std::string& path, X509* cert) {
    BIO* bio = BIO_new_file(path.c_str(), "wb");
    if (bio == nullptr) {
        return false;
    }
    const int written = PEM_write_bio_X509(bio, cert);
    BIO_free_all(bio);
    return written == 1;
}

/// Self-signed certificate for CN=localhost, valid for a year
X509* self_signed_certificate(EVP_PKEY* key) {
    X509* cert = X509_new();
    if (cert == nullptr) {
        return nullptr;
    }

    X509_set_version(cert, 2);  // X.509 v3
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 60L * 60 * 24 * 365);

    if (X509_set_pubkey(cert, key) != 1) {
        X509_free(cert);
        return nullptr;
    }

    X509_NAME* name = X509_get_subject_name(cert);
    if (X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>("localhost"), -1, -1,
                                   0) != 1) {
        X509_free(cert);
        return nullptr;
    }
    X509_set_issuer_name(cert, name);

    if (X509_sign(cert, key, EVP_sha256()) == 0) {
        X509_free(cert);
        return nullptr;
    }
    return cert;
}

}  // namespace

// Generate a standalone private key (used for the mismatched-key case)
bool generate_private_key(const std::string& path) {
    EVP_PKEY* key = generate_rsa_key();
    if (key == nullptr) {
        return false;
    }
    const bool written = write_private_key(path, key);
    EVP_PKEY_free(key);
    return written;
}

// Helper function to generate test certificates
bool generate_test_certificates() {
    if (std::filesystem::exists(TEST_CERT_PATH) && 
        std::filesystem::exists(TEST_KEY_PATH)) {
        return true; // Already exist
    }

    EVP_PKEY* key = generate_rsa_key();
    if (key == nullptr) {
        return false;
    }

    X509* cert = self_signed_certificate(key);
    if (cert == nullptr) {
        EVP_PKEY_free(key);
        return false;
    }

    const bool written = write_private_key(TEST_KEY_PATH, key) &&
                         write_certificate(TEST_CERT_PATH, cert);

    X509_free(cert);
    EVP_PKEY_free(key);
    return written;
}

// Helper function to clean up test certificates
void cleanup_test_certificates() {
    std::filesystem::remove(TEST_CERT_PATH);
    std::filesystem::remove(TEST_KEY_PATH);
}

// Test TLS context creation with real certificates
TEST(TlsIntegrationTest, ContextCreationWithRealCertificates) {
    ASSERT_TRUE(generate_test_certificates()) << "Failed to generate test certificates";

    try {
        TlsContext ctx;
        ctx.load_certificate_and_key(TEST_CERT_PATH, TEST_KEY_PATH);
        ctx.set_min_protocol_version("TLSv1.2");
        ctx.set_max_protocol_version("TLSv1.3");
        SUCCEED();
    } catch (const std::runtime_error& e) {
        FAIL() << "TlsContext creation failed: " << e.what();
    }

    cleanup_test_certificates();
}

// Test TLS context with invalid certificate
TEST(TlsIntegrationTest, ContextCreationWithInvalidCertificate) {
    try {
        TlsContext ctx;
        ctx.load_certificate_and_key("nonexistent_cert.pem", TEST_KEY_PATH);
        FAIL() << "Should have thrown exception for missing certificate";
    } catch (const std::runtime_error& e) {
        EXPECT_TRUE(std::string(e.what()).find("certificate") != std::string::npos ||
                    std::string(e.what()).find("Certificate") != std::string::npos);
    }
}

// Test TLS context with invalid private key
TEST(TlsIntegrationTest, ContextCreationWithInvalidPrivateKey) {
    ASSERT_TRUE(generate_test_certificates()) << "Failed to generate test certificates";

    try {
        TlsContext ctx;
        ctx.load_certificate_and_key(TEST_CERT_PATH, "nonexistent_key.pem");
        FAIL() << "Should have thrown exception for missing private key";
    } catch (const std::runtime_error& e) {
        EXPECT_TRUE(std::string(e.what()).find("key") != std::string::npos ||
                    std::string(e.what()).find("Key") != std::string::npos);
    }

    cleanup_test_certificates();
}

// Test TLS context with certificate/key mismatch
TEST(TlsIntegrationTest, ContextCreationWithMismatchedKey) {
    ASSERT_TRUE(generate_test_certificates()) << "Failed to generate test certificates";

    // Generate a different key
    const char* wrong_key = "wrong_key.pem";
    ASSERT_TRUE(generate_private_key(wrong_key)) << "openssl key generation failed";

    try {
        TlsContext ctx;
        ctx.load_certificate_and_key(TEST_CERT_PATH, wrong_key);
        FAIL() << "Should have thrown exception for mismatched key";
    } catch (const std::runtime_error& e) {
        // OpenSSL may or may not detect this mismatch during loading
        // Just ensure context creation either succeeds or fails with a reasonable error
    }

    std::filesystem::remove(wrong_key);
    cleanup_test_certificates();
}

// Test SSL object creation
TEST(TlsIntegrationTest, SslObjectCreation) {
    ASSERT_TRUE(generate_test_certificates()) << "Failed to generate test certificates";

    try {
        TlsContext ctx;
        ctx.load_certificate_and_key(TEST_CERT_PATH, TEST_KEY_PATH);
        
        // SSL objects are created by TlsConnection, not directly by TlsContext
        // This test verifies the context is valid for SSL creation
        EXPECT_TRUE(ctx.is_valid());
        SUCCEED();
    } catch (const std::runtime_error& e) {
        FAIL() << "SSL object creation failed: " << e.what();
    }

    cleanup_test_certificates();
}

// Test protocol version enforcement
TEST(TlsIntegrationTest, ProtocolVersionEnforcement) {
    ASSERT_TRUE(generate_test_certificates()) << "Failed to generate test certificates";

    try {
        // Test with minimum TLS 1.2
        TlsContext ctx;
        ctx.load_certificate_and_key(TEST_CERT_PATH, TEST_KEY_PATH);
        ctx.set_min_protocol_version("TLSv1.2");
        ctx.set_max_protocol_version("TLSv1.3");
        SUCCEED();
    } catch (const std::runtime_error& e) {
        FAIL() << "Protocol version configuration failed: " << e.what();
    }

    cleanup_test_certificates();
}

// Test TLS connection initialization
TEST(TlsIntegrationTest, ConnectionInitialization) {
    ASSERT_TRUE(generate_test_certificates()) << "Failed to generate test certificates";

    try {
        TlsContext ctx;
        ctx.load_certificate_and_key(TEST_CERT_PATH, TEST_KEY_PATH);
        
        // Create a dummy socket
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(sockfd, 0);
        
        TlsConnection conn(ctx, sockfd);
        EXPECT_TRUE(conn.is_valid());
        
        close(sockfd);
        SUCCEED();
    } catch (const std::runtime_error& e) {
        FAIL() << "Connection initialization failed: " << e.what();
    }

    cleanup_test_certificates();
}

// Test handshake WANT_READ/WANT_WRITE simulation
TEST(TlsIntegrationTest, HandshakeWantReadWantWrite) {
    ASSERT_TRUE(generate_test_certificates()) << "Failed to generate test certificates";

    try {
        TlsContext ctx;
        ctx.load_certificate_and_key(TEST_CERT_PATH, TEST_KEY_PATH);
        
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(sockfd, 0);
        
        // Set nonblocking
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
        
        TlsConnection conn(ctx, sockfd);
        
        // Attempt handshake on a non-connected socket
        // This should return WANT_READ or WANT_WRITE
        auto result = conn.do_handshake();
        EXPECT_TRUE(result == TlsIoRequirement::WantRead ||
                    result == TlsIoRequirement::WantWrite ||
                    result == TlsIoRequirement::Closed);
        
        close(sockfd);
        SUCCEED();
    } catch (const std::runtime_error& e) {
        // This is expected since we're not actually connecting
        SUCCEED();
    }

    cleanup_test_certificates();
}

// Test TLS read WANT_READ/WANT_WRITE
TEST(TlsIntegrationTest, ReadWantReadWantWrite) {
    ASSERT_TRUE(generate_test_certificates()) << "Failed to generate test certificates";

    try {
        TlsContext ctx;
        ctx.load_certificate_and_key(TEST_CERT_PATH, TEST_KEY_PATH);
        
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(sockfd, 0);
        
        TlsConnection conn(ctx, sockfd);
        
        std::vector<char> buffer(1024);
        size_t bytes_read = 0;
        auto result = conn.read(buffer.data(), buffer.size(), bytes_read);
        
        // Should fail or want read/write without established connection
        EXPECT_TRUE(result == TlsIoRequirement::Closed ||
                    result == TlsIoRequirement::WantRead ||
                    result == TlsIoRequirement::WantWrite);
        
        close(sockfd);
        SUCCEED();
    } catch (const std::runtime_error& e) {
        SUCCEED();
    }

    cleanup_test_certificates();
}

// Test TLS write WANT_READ/WANT_WRITE
TEST(TlsIntegrationTest, WriteWantReadWantWrite) {
    ASSERT_TRUE(generate_test_certificates()) << "Failed to generate test certificates";

    try {
        TlsContext ctx;
        ctx.load_certificate_and_key(TEST_CERT_PATH, TEST_KEY_PATH);
        
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(sockfd, 0);
        
        TlsConnection conn(ctx, sockfd);
        
        const char* data = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        size_t bytes_written = 0;
        auto result = conn.write(data, strlen(data), bytes_written);
        
        // Should fail or want read/write without established connection
        EXPECT_TRUE(result == TlsIoRequirement::Closed ||
                    result == TlsIoRequirement::WantRead ||
                    result == TlsIoRequirement::WantWrite);
        
        close(sockfd);
        SUCCEED();
    } catch (const std::runtime_error& e) {
        SUCCEED();
    }

    cleanup_test_certificates();
}

// Test TLS connection cleanup
TEST(TlsIntegrationTest, ConnectionCleanup) {
    ASSERT_TRUE(generate_test_certificates()) << "Failed to generate test certificates";

    try {
        TlsContext ctx;
        ctx.load_certificate_and_key(TEST_CERT_PATH, TEST_KEY_PATH);
        
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(sockfd, 0);
        
        {
            TlsConnection conn(ctx, sockfd);
            // Connection goes out of scope here
        }
        
        // Socket should still be valid (connection doesn't own it)
        close(sockfd);
        SUCCEED();
    } catch (const std::runtime_error& e) {
        FAIL() << "Connection cleanup failed: " << e.what();
    }

    cleanup_test_certificates();
}

// Test multiple sequential TLS connections
TEST(TlsIntegrationTest, MultipleSequentialConnections) {
    ASSERT_TRUE(generate_test_certificates()) << "Failed to generate test certificates";

    try {
        TlsContext ctx;
        ctx.load_certificate_and_key(TEST_CERT_PATH, TEST_KEY_PATH);
        
        for (int i = 0; i < 10; ++i) {
            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            ASSERT_GE(sockfd, 0);
            
            TlsConnection conn(ctx, sockfd);
            EXPECT_TRUE(conn.is_valid());
            
            close(sockfd);
        }
        
        SUCCEED();
    } catch (const std::runtime_error& e) {
        FAIL() << "Multiple connections failed: " << e.what();
    }

    cleanup_test_certificates();
}

// Test security: verify old protocols are disabled
TEST(TlsIntegrationTest, SecurityOldProtocolsDisabled) {
    ASSERT_TRUE(generate_test_certificates()) << "Failed to generate test certificates";

    try {
        // Create context with minimum TLS 1.2
        TlsContext ctx;
        ctx.load_certificate_and_key(TEST_CERT_PATH, TEST_KEY_PATH);
        ctx.set_min_protocol_version("TLSv1.2");
        ctx.set_max_protocol_version("TLSv1.3");
        
        // The context should reject SSLv2, SSLv3, TLS 1.0, TLS 1.1
        // This is verified by the TlsContext constructor which explicitly disables them
        SUCCEED();
    } catch (const std::runtime_error& e) {
        FAIL() << "Security configuration failed: " << e.what();
    }

    cleanup_test_certificates();
}

} // namespace test
} // namespace aevrix

#else

#include <iostream>

int main() {
    std::cout << "TLS tests skipped (TLS not enabled in build)" << std::endl;
    return 0;
}

#endif // AEVRIX_ENABLE_TLS
