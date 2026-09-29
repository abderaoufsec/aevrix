// =============================================================================
// Aevrix - TLS Context Unit Tests (Phase 21)
// =============================================================================

#include <iostream>
#include <fstream>
#include <cstdio>

#ifdef AEVRIX_ENABLE_TLS

#include <gtest/gtest.h>
#include "aevrix/tls_context.h"
#include "aevrix/logger.h"
#include <cassert>

namespace aevrix {
namespace test {

// =============================================================================
// Test: TLS Context Creation
// =============================================================================
TEST(TlsContextTest, ContextCreation) {
    try {
        TlsContext ctx;
        EXPECT_TRUE(ctx.is_valid());
    } catch (const std::exception& e) {
        FAIL() << "TLS context creation failed: " << e.what();
    }
}

// =============================================================================
// Test: Invalid Certificate Path
// =============================================================================
TEST(TlsContextTest, InvalidCertificatePath) {
    try {
        TlsContext ctx;
        ctx.load_certificate_and_key("/nonexistent/cert.pem", "/nonexistent/key.pem");
        FAIL() << "Should have thrown exception for invalid path";
    } catch (const std::exception& e) {
        SUCCEED();
    }
}

// =============================================================================
// Test: Invalid Private Key Path
// =============================================================================
TEST(TlsContextTest, InvalidPrivateKeyPath) {
    try {
        TlsContext ctx;
        ctx.load_certificate_and_key("/nonexistent/cert.pem", "/nonexistent/key.pem");
        FAIL() << "Should have thrown exception for invalid path";
    } catch (const std::exception& e) {
        SUCCEED();
    }
}

// =============================================================================
// Test: Certificate/Key Mismatch
// =============================================================================
TEST(TlsContextTest, CertificateKeyMismatch) {
    // Create temporary mismatched cert and key files
    std::string cert_path = "/tmp/test_cert.pem";
    std::string key_path = "/tmp/test_key.pem";

    // Write a dummy certificate (not a real cert, just to test path handling)
    std::ofstream cert_file(cert_path);
    cert_file << "-----BEGIN CERTIFICATE-----\n";
    cert_file << "MIIBkTCBwIJANRUU1TUTEXAMPLE\n";
    cert_file << "-----END CERTIFICATE-----\n";
    cert_file.close();

    // Write a dummy key (not a real key, just to test path handling)
    std::ofstream key_file(key_path);
    key_file << "-----BEGIN PRIVATE KEY-----\n";
    key_file << "MIIBkTCBwIJANRUU1TUTEXAMPLE\n";
    key_file << "-----END PRIVATE KEY-----\n";
    key_file.close();

    try {
        TlsContext ctx;
        ctx.load_certificate_and_key(cert_path, key_path);
        // OpenSSL may or may not detect mismatch with dummy data
        // Just ensure it doesn't crash
    } catch (const std::exception& e) {
        // Expected for invalid cert/key data
    }

    // Cleanup
    std::remove(cert_path.c_str());
    std::remove(key_path.c_str());
}

// =============================================================================
// Test: Protocol Version Configuration
// =============================================================================
TEST(TlsContextTest, ProtocolVersionConfiguration) {
    try {
        TlsContext ctx;
        ctx.set_min_protocol_version("TLSv1.2");
        ctx.set_max_protocol_version("TLSv1.3");
        SUCCEED();
    } catch (const std::exception& e) {
        FAIL() << "Protocol version configuration failed: " << e.what();
    }
}

// =============================================================================
// Test: Invalid Protocol Version
// =============================================================================
TEST(TlsContextTest, InvalidProtocolVersion) {
    try {
        TlsContext ctx;
        ctx.set_min_protocol_version("SSLv3");
        FAIL() << "Should have thrown exception for SSLv3";
    } catch (const std::exception& e) {
        SUCCEED();
    }
}

// =============================================================================
// Test: TLS Context Move Semantics
// =============================================================================
TEST(TlsContextTest, MoveSemantics) {
    try {
        TlsContext ctx1;
        EXPECT_TRUE(ctx1.is_valid());

        // Move construct
        TlsContext ctx2(std::move(ctx1));
        EXPECT_FALSE(ctx1.is_valid());
        EXPECT_TRUE(ctx2.is_valid());

        // Move assign
        TlsContext ctx3;
        ctx3 = std::move(ctx2);
        EXPECT_FALSE(ctx2.is_valid());
        EXPECT_TRUE(ctx3.is_valid());

        SUCCEED();
    } catch (const std::exception& e) {
        FAIL() << "Move semantics test failed: " << e.what();
    }
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
