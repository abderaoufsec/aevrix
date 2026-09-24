// =============================================================================
// Aevrix - Main Entry Point
// =============================================================================
// This file implements the main entry point for the Aevrix HTTP server.
// In Phase 3, we implement structured HTTP response serialization using the
// HttpResponse class and HttpResponseSerializer.
//
// Current Implementation (Phase 3):
// - Create TCP listener on 127.0.0.1:8080
// - Accept incoming connections
// - Build structured HTTP responses using HttpResponse class
// - Serialize responses to wire format using HttpResponseSerializer
// - Send serialized responses to clients
// - Close the connection
//
// Previous Phases:
// - Phase 1: RAII file descriptors (UniqueFd)
// - Phase 2: TCP listener with socket/bind/listen/accept
//
// Future Phases Will Add:
// - Phase 4: HTTP request parsing
// - Phase 5: Full request/response pipeline
// - Phase 8: Non-blocking I/O with epoll
// =============================================================================

#include "aevrix/tcp_listener.h"
#include "aevrix/unique_fd.h"
#include "aevrix/http_response.h"
#include "aevrix/http_response_serializer.h"
#include <iostream>
#include <string>
#include <cstdint>  // For uint16_t

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <cerrno>
#endif

// Use the http namespace for convenience
using namespace aevrix::http;

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
bool send_data(int client_fd, const char* data, size_t length) {
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
        // In Phase 3, we'll just log this. Phase 5 will handle partial sends properly.
    }

    return true;
}

/**
 * @brief Build a structured HTTP response
 * 
 * Creates an HttpResponse object with proper status, headers, and body.
// In Phase 3, this is a simple 200 OK response with plain text.
// Future phases will make this more sophisticated based on requests.
 * 
 * @return HttpResponse The structured HTTP response
 */
HttpResponse build_response() {
    // Create a 200 OK response
    HttpResponse response(StatusCode::OK, "Hello from Aevrix!");
    
    // Set common headers
    response.set_header("Content-Type", "text/plain");
    response.set_header("Server", "Aevrix/0.1.0");
    
    // Set connection policy to close for Phase 3 (single request per connection)
    // Phase 7 will implement keep-alive support
    response.set_connection_policy(ConnectionPolicy::Close);
    
    return response;
}

/**
 * @brief Handle a single client connection
 * 
 * Accepts a connection, builds a structured HTTP response, serializes it,
// and sends it to the client. This is the Phase 3 implementation using
// the HttpResponse class and HttpResponseSerializer.
 * 
 * @param client_fd The client socket descriptor
 */
void handle_connection(int client_fd) {
    std::cout << "Handling client connection...\n";

    try {
        // Build a structured HTTP response
        HttpResponse response = build_response();
        
        // Serialize the response to wire format
        std::string serialized_response = HttpResponseSerializer::serialize(response);
        
        if (serialized_response.empty()) {
            std::cerr << "Failed to serialize response\n";
            return;
        }

        std::cout << "Sending response (" << serialized_response.length() << " bytes)...\n";

        // Send the serialized response
        if (send_data(client_fd, serialized_response.c_str(), serialized_response.length())) {
            std::cout << "Response sent successfully\n";
            std::cout << "Response content:\n" << serialized_response << "\n";
        } else {
            std::cerr << "Failed to send response\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "Exception in handle_connection: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "Unknown exception in handle_connection\n";
    }

    // Close the client connection using UniqueFd for automatic cleanup
    aevrix::UniqueFd client_unique_fd(client_fd);
    // client_unique_fd will automatically close the descriptor when it goes out of scope
    
    std::cout << "Connection closed\n";
}

/**
 * @brief Main entry point for the Aevrix HTTP server
 * 
 * Creates a TCP listener, accepts connections, and handles them with structured
// HTTP responses using the HttpResponse class and HttpResponseSerializer.
// This is the Phase 3 implementation - proper HTTP response serialization.
 * 
 * Usage:
 *   ./aevrix
 *   # Server will listen on 127.0.0.1:8080
 *   # Test with: curl http://127.0.0.1:8080/
 * 
 * @return int Exit code (0 for success, non-zero for error)
 */
int main() {
    std::cout << "=== Aevrix HTTP Server - Phase 3 ===\n";
    std::cout << "Structured HTTP Response Serialization\n\n";

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
        std::cout << "Using structured HTTP responses with HttpResponse class\n";
        std::cout << "Press Ctrl+C to stop\n\n";

        // Main server loop
        // In Phase 3, this is still a simple blocking loop
        // Phase 8 will replace this with epoll-based event loop
        int connection_count = 0;
        const int MAX_CONNECTIONS = 5; // Limit for Phase 3 testing

        while (connection_count < MAX_CONNECTIONS) {
            std::cout << "Waiting for connection (" 
                      << (connection_count + 1) << "/" << MAX_CONNECTIONS << ")...\n";

            // Accept a connection (blocking call)
            auto client_fd = listener.accept();
            
            if (client_fd.has_value()) {
                // Handle the connection with structured HTTP response
                handle_connection(client_fd.value());
                connection_count++;
            } else {
                std::cerr << "Failed to accept connection\n";
                break;
            }
        }

        std::cout << "\nPhase 3 test complete. Accepted " << connection_count 
                  << " connections with structured responses.\n";
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
