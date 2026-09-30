// =============================================================================
// Aevrix - TLS Connection Unit Tests (Phase 21)
// =============================================================================

#include <iostream>
#include <cassert>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#ifdef AEVRIX_ENABLE_TLS

#include <gtest/gtest.h>
#include "aevrix/tls_connection.h"
#include "aevrix/tls_context.h"
#include "aevrix/logger.h"

namespace aevrix {
namespace test {

// =============================================================================
// Helper: Create a nonblocking socket for testing
// =============================================================================
int create_test_socket() {
#ifdef _WIN32
    SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) {
        return -1;
    }

    // Set nonblocking
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
    return static_cast<int>(fd);
#else
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    // Set nonblocking
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        close(fd);
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        close(fd);
        return -1;
    }

    return fd;
#endif
}

// =============================================================================
// Test: TLS Connection Creation
// =============================================================================
TEST(TlsConnectionTest, ConnectionCreation) {
    try {
        TlsContext ctx;
        int fd = create_test_socket();
        ASSERT_GE(fd, 0) << "Socket creation failed";

        TlsConnection conn(ctx, fd);
        EXPECT_TRUE(conn.is_valid());
        EXPECT_EQ(conn.handshake_state(), TlsHandshakeState::NotStarted);

        close(fd);
    } catch (const std::exception& e) {
        FAIL() << "Connection creation failed: " << e.what();
    }
}

// =============================================================================
// Test: TLS Connection Move Semantics
// =============================================================================
TEST(TlsConnectionTest, MoveSemantics) {
    try {
        TlsContext ctx;
        int fd = create_test_socket();
        ASSERT_GE(fd, 0) << "Socket creation failed";

        TlsConnection conn1(ctx, fd);
        EXPECT_TRUE(conn1.is_valid());

        // Move construct
        TlsConnection conn2(std::move(conn1));
        EXPECT_FALSE(conn1.is_valid());
        EXPECT_TRUE(conn2.is_valid());

        // Move assign into an existing connection (TlsConnection has no default ctor)
        int fd2 = create_test_socket();
        ASSERT_GE(fd2, 0) << "Second socket creation failed";
        TlsConnection conn3(ctx, fd2);
        EXPECT_TRUE(conn3.is_valid());

        conn3 = std::move(conn2);
        EXPECT_FALSE(conn2.is_valid());
        EXPECT_TRUE(conn3.is_valid());

        close(fd);
        close(fd2);
    } catch (const std::exception& e) {
        FAIL() << "Move semantics test failed: " << e.what();
    }
}

// =============================================================================
// Test: Handshake State Tracking
// =============================================================================
TEST(TlsConnectionTest, HandshakeStateTracking) {
    try {
        TlsContext ctx;
        int fd = create_test_socket();
        ASSERT_GE(fd, 0) << "Socket creation failed";

        TlsConnection conn(ctx, fd);
        EXPECT_EQ(conn.handshake_state(), TlsHandshakeState::NotStarted);

        // Attempt handshake (will fail without a real peer, but should track state)
        [[maybe_unused]] TlsIoRequirement io_req = conn.do_handshake();
        // Handshake will fail or need I/O, but state should change
        EXPECT_TRUE(conn.handshake_state() == TlsHandshakeState::InProgress || 
                    conn.handshake_state() == TlsHandshakeState::Failed);
        (void)io_req;

        close(fd);
    } catch (const std::exception& e) {
        FAIL() << "Handshake state tracking failed: " << e.what();
    }
}

// =============================================================================
// Test: TLS Read (no peer)
// =============================================================================
TEST(TlsConnectionTest, ReadNoPeer) {
    try {
        TlsContext ctx;
        int fd = create_test_socket();
        ASSERT_GE(fd, 0) << "Socket creation failed";

        TlsConnection conn(ctx, fd);
        char buffer[1024];
        size_t bytes_read = 0;

        TlsIoRequirement io_req = conn.read(buffer, sizeof(buffer), bytes_read);
        // Should fail or need I/O since no peer
        EXPECT_TRUE(io_req == TlsIoRequirement::Closed || 
                    io_req == TlsIoRequirement::WantRead ||
                    io_req == TlsIoRequirement::WantWrite);

        close(fd);
    } catch (const std::exception& e) {
        FAIL() << "TLS read test failed: " << e.what();
    }
}

// =============================================================================
// Test: TLS Write (no peer)
// =============================================================================
TEST(TlsConnectionTest, WriteNoPeer) {
    try {
        TlsContext ctx;
        int fd = create_test_socket();
        ASSERT_GE(fd, 0) << "Socket creation failed";

        TlsConnection conn(ctx, fd);
        const char* data = "test";
        size_t bytes_written = 0;

        TlsIoRequirement io_req = conn.write(data, 4, bytes_written);
        // Should fail or need I/O since no peer
        EXPECT_TRUE(io_req == TlsIoRequirement::Closed ||
                    io_req == TlsIoRequirement::WantRead ||
                    io_req == TlsIoRequirement::WantWrite);

        close(fd);
    } catch (const std::exception& e) {
        FAIL() << "TLS write test failed: " << e.what();
    }
}

// =============================================================================
// Test: TLS Shutdown
// =============================================================================
TEST(TlsConnectionTest, Shutdown) {
    try {
        TlsContext ctx;
        int fd = create_test_socket();
        ASSERT_GE(fd, 0) << "Socket creation failed";

        TlsConnection conn(ctx, fd);
        TlsIoRequirement io_req = conn.shutdown();
        // Should handle shutdown gracefully
        EXPECT_TRUE(io_req == TlsIoRequirement::Closed ||
                    io_req == TlsIoRequirement::WantRead ||
                    io_req == TlsIoRequirement::WantWrite);

        close(fd);
    } catch (const std::exception& e) {
        FAIL() << "TLS shutdown test failed: " << e.what();
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
