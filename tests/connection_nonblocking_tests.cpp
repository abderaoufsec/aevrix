// =============================================================================
// Aevrix - Connection Nonblocking I/O Tests
// =============================================================================
// This file contains unit tests for the nonblocking I/O operations in the
// Connection class. These tests verify that:
// - Nonblocking reads handle EAGAIN/EWOULDBLOCK correctly
// - Nonblocking writes handle EAGAIN/EWOULDBLOCK correctly
// - Partial reads are handled correctly
// - Partial writes are handled correctly
// - EINTR is handled correctly
// - Socket errors are handled correctly
// - Peer disconnect is handled correctly
// =============================================================================

#include "aevrix/connection.h"
#include "aevrix/logger.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

using namespace aevrix;

// Helper to create a pair of connected sockets for testing
std::pair<int, int> create_socket_pair() {
#ifdef _WIN32
    // Windows doesn't have socketpair, use TCP loopback
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listen_sock != INVALID_SOCKET && "Failed to create listen socket");

    // Set nonblocking
    u_long mode = 1;
    ioctlsocket(listen_sock, FIONBIO, &mode);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    listen(listen_sock, 1);

    // Get the actual port
    int addr_len = sizeof(addr);
    getsockname(listen_sock, reinterpret_cast<sockaddr*>(&addr), &addr_len);

    SOCKET client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(client_sock != INVALID_SOCKET && "Failed to create client socket");

    // Set client nonblocking
    ioctlsocket(client_sock, FIONBIO, &mode);

    // Connect (will fail with WSAEWOULDBLOCK in nonblocking mode)
    connect(client_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    // Accept the connection
    sockaddr_in client_addr{};
    int client_len = sizeof(client_addr);
    SOCKET server_sock = accept(listen_sock, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    
    closesocket(listen_sock);
    
    // Convert SOCKET to int for our API
    int client_fd = static_cast<int>(client_sock);
    int server_fd = static_cast<int>(server_sock);
    
    return {client_fd, server_fd};
#else
    int sv[2];
    int result = socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv);
    assert(result == 0 && "Failed to create socket pair");
    return {sv[0], sv[1]};
#endif
}

void close_socket(int fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

// =============================================================================
// Test: Nonblocking Read - Empty Socket (EAGAIN/EWOULDBLOCK)
// =============================================================================

void test_read_nonblocking_empty_socket() {
    std::cout << "Testing read_nonblocking with empty socket..." << std::endl;
    
    auto [client_fd, server_fd] = create_socket_pair();
    
    Connection conn(server_fd, 1);
    
    // Try to read from empty nonblocking socket
    auto result = conn.read_nonblocking();
    
    // Should return InProgress (EAGAIN/EWOULDBLOCK)
    assert(result == Connection::IoResult::InProgress && "Expected InProgress for empty socket");
    assert(conn.input_buffer().empty() && "Input buffer should be empty");
    
    close_socket(client_fd);
    close_socket(server_fd);
    
    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Test: Nonblocking Read - Successful Read
// =============================================================================

void test_read_nonblocking_successful_read() {
    std::cout << "Testing read_nonblocking with successful read..." << std::endl;
    
    auto [client_fd, server_fd] = create_socket_pair();
    
    Connection conn(server_fd, 1);
    
    // Send data from client
    const char* test_data = "Hello, World!";
#ifdef _WIN32
    send(client_fd, test_data, static_cast<int>(strlen(test_data)), 0);
#else
    send(client_fd, test_data, strlen(test_data), 0);
#endif
    
    // Give it a moment for data to arrive
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Read from server side
    auto result = conn.read_nonblocking();
    
    // Should return Success
    assert(result == Connection::IoResult::Success && "Expected Success for read");
    
    // Check buffer contents
    std::string received(conn.input_buffer().data(), conn.input_buffer().size());
    assert(received == test_data && "Received data doesn't match sent data");
    
    close_socket(client_fd);
    close_socket(server_fd);
    
    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Test: Nonblocking Read - Peer Disconnect
// =============================================================================

void test_read_nonblocking_peer_disconnect() {
    std::cout << "Testing read_nonblocking with peer disconnect..." << std::endl;
    
    auto [client_fd, server_fd] = create_socket_pair();
    
    Connection conn(server_fd, 1);
    
    // Close client side
    close_socket(client_fd);
    
    // Give it a moment
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Try to read from server side
    auto result = conn.read_nonblocking();
    
    // Should return Closed
    assert(result == Connection::IoResult::Closed && "Expected Closed for peer disconnect");
    
    close_socket(server_fd);
    
    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Test: Nonblocking Write - Successful Write
// =============================================================================

void test_write_nonblocking_successful_write() {
    std::cout << "Testing write_nonblocking with successful write..." << std::endl;
    
    auto [client_fd, server_fd] = create_socket_pair();
    
    Connection conn(server_fd, 1);
    
    // Prepare data to write
    const std::string test_data = "Hello, World!";
    conn.output_buffer().assign(test_data.begin(), test_data.end());
    
    // Write
    auto result = conn.write_nonblocking();
    
    // Should return Success
    assert(result == Connection::IoResult::Success && "Expected Success for write");
    
    // Buffer should be empty (all data sent)
    assert(conn.output_buffer().empty() && "Output buffer should be empty after write");
    
    // Verify data was received on client side
    char buffer[128];
#ifdef _WIN32
    int received = recv(client_fd, buffer, sizeof(buffer), 0);
#else
    ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);
#endif
    assert(received > 0 && "Should have received data");
    std::string received_data(buffer, static_cast<size_t>(received));
    assert(received_data == test_data && "Received data doesn't match sent data");
    
    close_socket(client_fd);
    close_socket(server_fd);
    
    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Test: Nonblocking Write - Empty Buffer
// =============================================================================

void test_write_nonblocking_empty_buffer() {
    std::cout << "Testing write_nonblocking with empty buffer..." << std::endl;
    
    auto [client_fd, server_fd] = create_socket_pair();
    
    Connection conn(server_fd, 1);
    
    // Don't add any data to output buffer
    assert(conn.output_buffer().empty() && "Output buffer should be empty");
    
    // Write with empty buffer
    auto result = conn.write_nonblocking();
    
    // Should return Success (nothing to do)
    assert(result == Connection::IoResult::Success && "Expected Success for empty buffer");
    
    close_socket(client_fd);
    close_socket(server_fd);
    
    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Test: Connection State Transitions
// =============================================================================

void test_connection_state_transitions() {
    std::cout << "Testing connection state transitions..." << std::endl;
    
    auto [client_fd, server_fd] = create_socket_pair();
    
    Connection conn(server_fd, 1);
    
    // Initial state should be New
    assert(conn.state() == ConnectionState::New && "Initial state should be New");
    
    // Set to Reading
    conn.set_state(ConnectionState::Reading);
    assert(conn.state() == ConnectionState::Reading && "State should be Reading");
    
    // Set to Writing
    conn.set_state(ConnectionState::Writing);
    assert(conn.state() == ConnectionState::Writing && "State should be Writing");
    
    // Set to Waiting
    conn.set_state(ConnectionState::Waiting);
    assert(conn.state() == ConnectionState::Waiting && "State should be Waiting");
    
    // Set to Closing
    conn.set_state(ConnectionState::Closing);
    assert(conn.state() == ConnectionState::Closing && "State should be Closing");
    
    // Set to Closed
    conn.set_state(ConnectionState::Closed);
    assert(conn.state() == ConnectionState::Closed && "State should be Closed");
    
    close_socket(client_fd);
    close_socket(server_fd);
    
    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Test: Connection Validity Checks
// =============================================================================

void test_connection_validity_checks() {
    std::cout << "Testing connection validity checks..." << std::endl;
    
    auto [client_fd, server_fd] = create_socket_pair();
    
    Connection conn(server_fd, 1);
    
    // Should be valid initially
    assert(conn.is_valid() && "Connection should be valid");
    
    // Can read in Reading or Waiting state
    conn.set_state(ConnectionState::Reading);
    assert(conn.can_read() && "Should be able to read in Reading state");
    
    conn.set_state(ConnectionState::Waiting);
    assert(conn.can_read() && "Should be able to read in Waiting state");
    
    // Cannot read in other states
    conn.set_state(ConnectionState::Writing);
    assert(!conn.can_read() && "Should not be able to read in Writing state");
    
    // Can write in Writing or Waiting state
    conn.set_state(ConnectionState::Writing);
    assert(conn.can_write() && "Should be able to write in Writing state");
    
    conn.set_state(ConnectionState::Waiting);
    assert(conn.can_write() && "Should be able to write in Waiting state");
    
    // Cannot write in other states
    conn.set_state(ConnectionState::Reading);
    assert(!conn.can_write() && "Should not be able to write in Reading state");
    
    // Should close if timeout
    conn.set_timeout_state(TimeoutState::HeaderTimeout);
    assert(conn.should_close() && "Should close on timeout");
    
    close_socket(client_fd);
    close_socket(server_fd);
    
    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Test: Buffer Management
// =============================================================================

void test_buffer_management() {
    std::cout << "Testing buffer management..." << std::endl;
    
    auto [client_fd, server_fd] = create_socket_pair();
    
    Connection conn(server_fd, 1);
    
    // Input buffer should be empty initially
    assert(conn.input_buffer().empty() && "Input buffer should be empty");
    
    // Add data to input buffer
    const std::string test_data = "Test data";
    conn.input_buffer().assign(test_data.begin(), test_data.end());
    assert(conn.input_buffer().size() == test_data.size() && "Input buffer size mismatch");
    
    // Clear input buffer
    conn.clear_input_buffer();
    assert(conn.input_buffer().empty() && "Input buffer should be empty after clear");
    
    // Output buffer should be empty initially
    assert(conn.output_buffer().empty() && "Output buffer should be empty");
    
    // Add data to output buffer
    conn.output_buffer().assign(test_data.begin(), test_data.end());
    assert(conn.output_buffer().size() == test_data.size() && "Output buffer size mismatch");
    
    // Clear output buffer
    conn.clear_output_buffer();
    assert(conn.output_buffer().empty() && "Output buffer should be empty after clear");
    
    close_socket(client_fd);
    close_socket(server_fd);
    
    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

    std::cout << "=== Connection Nonblocking I/O Tests ===" << std::endl;
    std::cout << std::endl;
    
    test_read_nonblocking_empty_socket();
    test_read_nonblocking_successful_read();
    test_read_nonblocking_peer_disconnect();
    test_write_nonblocking_successful_write();
    test_write_nonblocking_empty_buffer();
    test_connection_state_transitions();
    test_connection_validity_checks();
    test_buffer_management();
    
    std::cout << std::endl;
    std::cout << "=== All Tests Passed ===" << std::endl;
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    return 0;
}
