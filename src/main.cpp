// =============================================================================
// Aevrix - Main Entry Point
// =============================================================================
// This file implements the main entry point for the Aevrix HTTP server.
// In Phase 2, we implement a basic TCP listener that can accept connections
// and return a minimal HTTP response.
//
// Current Implementation (Phase 2):
// - Create TCP listener on 127.0.0.1:8080
// - Accept incoming connections
// - Send a minimal HTTP response
// - Close the connection
//
// Future Phases Will Add:
// - Phase 3: Proper HTTP response serialization
// - Phase 4: HTTP request parsing
// - Phase 5: Full request/response pipeline
// - Phase 8: Non-blocking I/O with epoll
// =============================================================================

#include "aevrix/tcp_listener.h"
#include "aevrix/unique_fd.h"
#include <iostream>
#include <string>
#include <cstring>
#include <cstdint>  // For uint16_t

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <cerrno>
#endif

/**
 * @brief Minimal HTTP response for testing
 * 
 * This is a hard-coded HTTP response for Phase 2 testing.
 * In Phase 3, this will be replaced with proper Response serialization.
 */
const char* MINIMAL_HTTP_RESPONSE = 
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 18\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Hello from Aevrix!";

/**
 * @brief Send data to a client socket
 * 
 * Sends the given data to the specified client socket descriptor.
 * Handles cross-platform send() differences.
 * 
 * @param client_fd The client socket descriptor
 * @param data The data to send
 * @param length The length of the data
 * @return true if successful, false on error
 */
bool send_response(int client_fd, const char* data, size_t length) {
#ifdef _WIN32
    SOCKET sock = static_cast<SOCKET>(client_fd);
    int sent = send(sock, data, static_cast<int>(length), 0);
    if (sent == SOCKET_ERROR) {
        std::cerr << "send() failed: " << WSAGetLastError() << "\n";
        return false;
    }
#else
    ssize_t sent = send(client_fd, data, length, 0);
    if (sent < 0) {
        std::cerr << "send() failed: " << strerror(errno) << "\n";
        return false;
    }
#endif

    // Check if we sent all the data
    if (static_cast<size_t>(sent) != length) {
        std::cerr << "Partial send: sent " << sent << " of " << length << " bytes\n";
        // In Phase 2, we'll just log this. Phase 5 will handle partial sends properly.
    }

    return true;
}

/**
 * @brief Handle a single client connection
 * 
 * Accepts a connection, sends a minimal HTTP response, and closes the connection.
 * This is a simplified version for Phase 2 testing.
 * 
 * @param client_fd The client socket descriptor
 */
void handle_connection(int client_fd) {
    std::cout << "Handling client connection...\n";

    // Send the minimal HTTP response
    size_t response_length = strlen(MINIMAL_HTTP_RESPONSE);
    if (send_response(client_fd, MINIMAL_HTTP_RESPONSE, response_length)) {
        std::cout << "Response sent successfully\n";
    } else {
        std::cerr << "Failed to send response\n";
    }

    // Close the client connection
    // In Phase 2, we use UniqueFd for automatic cleanup
    aevrix::UniqueFd client_unique_fd(client_fd);
    // client_unique_fd will automatically close the descriptor when it goes out of scope
    
    std::cout << "Connection closed\n";
}

/**
 * @brief Main entry point for the Aevrix HTTP server
 * 
 * Creates a TCP listener, accepts connections, and handles them with minimal responses.
 * This is the Phase 2 implementation - a basic "walking skeleton" server.
 * 
 * Usage:
 *   ./aevrix
 *   # Server will listen on 127.0.0.1:8080
 *   # Test with: curl http://127.0.0.1:8080/
 * 
 * @return int Exit code (0 for success, non-zero for error)
 */
int main() {
    std::cout << "=== Aevrix HTTP Server - Phase 2 ===\n";
    std::cout << "Starting TCP listener implementation\n\n";

    try {
        // Create TCP listener on localhost:8080
        // In future phases, this will be configurable via command-line arguments
        const std::string host = "127.0.0.1";
        const uint16_t port = 8080;

        aevrix::TcpListener listener(host, port);
        
        if (!listener.is_listening()) {
            std::cerr << "Failed to start TCP listener\n";
            std::cerr << "Error: " << listener.error_message() << "\n";
            return 1;
        }

        std::cout << "\nServer running on http://" << host << ":" << port << "/\n";
        std::cout << "Press Ctrl+C to stop\n\n";

        // Main server loop
        // In Phase 2, this is a simple blocking loop
        // Phase 8 will replace this with epoll-based event loop
        int connection_count = 0;
        const int MAX_CONNECTIONS = 5; // Limit for Phase 2 testing

        while (connection_count < MAX_CONNECTIONS) {
            std::cout << "Waiting for connection (" 
                      << (connection_count + 1) << "/" << MAX_CONNECTIONS << ")...\n";

            // Accept a connection (blocking call)
            auto client_fd = listener.accept();
            
            if (client_fd.has_value()) {
                // Handle the connection
                handle_connection(client_fd.value());
                connection_count++;
            } else {
                std::cerr << "Failed to accept connection\n";
                break;
            }
        }

        std::cout << "\nPhase 2 test complete. Accepted " << connection_count 
                  << " connections.\n";
        std::cout << "Stopping server...\n";

        // Stop the listener (will close the listening socket)
        listener.stop();

        std::cout << "Server stopped gracefully\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception occurred\n";
        return 1;
    }
}
