// =============================================================================
// Aevrix - Main Entry Point
// =============================================================================
// This file implements the main entry point for the Aevrix HTTP server.
// In Phase 4, we implement HTTP request parsing using the HttpRequestParser class.
//
// Current Implementation (Phase 4):
// - Create TCP listener on 127.0.0.1:8080
// - Accept incoming connections
// - Read HTTP requests from clients
// - Parse requests using HttpRequestParser
// - Generate structured HTTP responses based on requests
// - Send serialized responses to clients
// - Close the connection
//
// Previous Phases:
// - Phase 1: RAII file descriptors (UniqueFd)
// - Phase 2: TCP listener with socket/bind/listen/accept
// - Phase 3: Structured HTTP response serialization
//
// Future Phases Will Add:
// - Phase 5: Full request/response pipeline with routing
// - Phase 8: Non-blocking I/O with epoll
// =============================================================================

#include "aevrix/tcp_listener.h"
#include "aevrix/unique_fd.h"
#include "aevrix/http_response.h"
#include "aevrix/http_response_serializer.h"
#include "aevrix/http_request_parser.h"
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
 * @brief Receive data from a client socket
 * 
 * Receives data from the specified client socket descriptor.
// Handles cross-platform recv() differences.
 * 
 * @param client_fd The client socket descriptor
 * @param buffer Buffer to store received data
 * @param buffer_size Size of the buffer
 * @return ssize_t Number of bytes received, or -1 on error
 */
ssize_t receive_data(int client_fd, char* buffer, size_t buffer_size) {
#ifdef _WIN32
    SOCKET sock = static_cast<SOCKET>(client_fd);
    int received = recv(sock, buffer, static_cast<int>(buffer_size), 0);
    if (received == SOCKET_ERROR) {
        return -1;
    }
#else
    ssize_t received = recv(client_fd, buffer, buffer_size, 0);
    if (received < 0) {
        return -1;
    }
#endif
    return received;
}

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
        // In Phase 4, we'll just log this. Phase 5 will handle partial sends properly.
    }

    return true;
}

/**
 * @brief Build an HTTP response based on the request
 * 
 * Generates an appropriate HTTP response based on the parsed request.
// In Phase 4, this is a simple implementation that returns 200 OK for GET requests
// and 405 Method Not Allowed for other methods.
 * 
 * @param request The parsed HTTP request
 * @return HttpResponse The structured HTTP response
 */
HttpResponse build_response(const HttpRequest& request) {
    // Check the method and respond accordingly
    if (request.method() == HttpMethod::GET) {
        // Return 200 OK for GET requests
        HttpResponse response(StatusCode::OK, "Hello from Aevrix!");
        response.set_header("Content-Type", "text/plain");
        response.set_header("Server", "Aevrix/0.1.0");
        response.set_connection_policy(ConnectionPolicy::Close);
        return response;
    } else if (request.method() == HttpMethod::HEAD) {
        // Return 200 OK with no body for HEAD requests
        HttpResponse response(StatusCode::OK);
        response.set_header("Content-Type", "text/plain");
        response.set_header("Content-Length", "18");  // Same as GET but no body
        response.set_header("Server", "Aevrix/0.1.0");
        response.set_connection_policy(ConnectionPolicy::Close);
        return response;
    } else {
        // Return 405 Method Not Allowed for unsupported methods
        HttpResponse response(StatusCode::MethodNotAllowed, 
                               "Method not allowed: " + aevrix::http::http_method_to_string(request.method()));
        response.set_header("Content-Type", "text/plain");
        response.set_header("Server", "Aevrix/0.1.0");
        response.set_header("Allow", "GET, HEAD");  // Indicate allowed methods
        response.set_connection_policy(ConnectionPolicy::Close);
        return response;
    }
}

/**
 * @brief Handle a single client connection
 * 
 * Accepts a connection, reads the HTTP request, parses it, generates a response,
// and sends it to the client. This is the Phase 4 implementation using the
// HttpRequestParser class.
 * 
 * @param client_fd The client socket descriptor
 */
void handle_connection(int client_fd) {
    std::cout << "Handling client connection...\n";

    try {
        // Buffer for receiving request data
        constexpr size_t BUFFER_SIZE = 8192;
        char buffer[BUFFER_SIZE];
        
        // Receive data from client
        ssize_t received = receive_data(client_fd, buffer, BUFFER_SIZE);
        
        if (received <= 0) {
            std::cerr << "Failed to receive data or connection closed\n";
            return;
        }

        std::cout << "Received " << received << " bytes from client\n";

        // Parse the HTTP request
        HttpRequestParser parser;
        parser.feed(buffer, received);

        if (parser.has_error()) {
            std::cerr << "Request parsing error: " << parser.error_message() << "\n";
            
            // Send 400 Bad Request for parsing errors
            HttpResponse error_response(StatusCode::BadRequest, "Bad Request");
            error_response.set_header("Content-Type", "text/plain");
            error_response.set_header("Server", "Aevrix/0.1.0");
            error_response.set_connection_policy(ConnectionPolicy::Close);
            
            std::string serialized = HttpResponseSerializer::serialize(error_response);
            send_data(client_fd, serialized.c_str(), serialized.length());
            return;
        }

        if (!parser.is_complete()) {
            std::cerr << "Incomplete request received\n";
            
            // Send 400 Bad Request for incomplete requests
            HttpResponse error_response(StatusCode::BadRequest, "Incomplete Request");
            error_response.set_header("Content-Type", "text/plain");
            error_response.set_header("Server", "Aevrix/0.1.0");
            error_response.set_connection_policy(ConnectionPolicy::Close);
            
            std::string serialized = HttpResponseSerializer::serialize(error_response);
            send_data(client_fd, serialized.c_str(), serialized.length());
            return;
        }

        // Get the parsed request
        const HttpRequest& request = parser.request();
        
        std::cout << "Parsed request: " << request.request_line() << "\n";
        std::cout << "Method: " << aevrix::http::http_method_to_string(request.method()) << "\n";
        std::cout << "Target: " << request.target() << "\n";
        std::cout << "Headers: " << request.headers().size() << "\n";

        // Build response based on request
        HttpResponse response = build_response(request);
        
        // Serialize the response
        std::string serialized_response = HttpResponseSerializer::serialize(response);
        
        if (serialized_response.empty()) {
            std::cerr << "Failed to serialize response\n";
            return;
        }

        std::cout << "Sending response (" << serialized_response.length() << " bytes)...\n";

        // Send the response
        if (send_data(client_fd, serialized_response.c_str(), serialized_response.length())) {
            std::cout << "Response sent successfully\n";
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
 * Creates a TCP listener, accepts connections, reads and parses HTTP requests,
// and handles them with structured HTTP responses using the HttpRequestParser class.
// This is the Phase 4 implementation - proper HTTP request parsing.
 * 
 * Usage:
 *   ./aevrix
 *   # Server will listen on 127.0.0.1:8080
 *   # Test with: curl http://127.0.0.1:8080/
 * 
 * @return int Exit code (0 for success, non-zero for error)
 */
int main() {
    std::cout << "=== Aevrix HTTP Server - Phase 4 ===\n";
    std::cout << "HTTP Request Parsing\n\n";

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
        std::cout << "Using HTTP request parser with HttpRequestParser class\n";
        std::cout << "Press Ctrl+C to stop\n\n";

        // Main server loop
        // In Phase 4, this is still a simple blocking loop
        // Phase 8 will replace this with epoll-based event loop
        int connection_count = 0;
        const int MAX_CONNECTIONS = 5; // Limit for Phase 4 testing

        while (connection_count < MAX_CONNECTIONS) {
            std::cout << "Waiting for connection (" 
                      << (connection_count + 1) << "/" << MAX_CONNECTIONS << ")...\n";

            // Accept a connection (blocking call)
            auto client_fd = listener.accept();
            
            if (client_fd.has_value()) {
                // Handle the connection with HTTP request parsing
                handle_connection(client_fd.value());
                connection_count++;
            } else {
                std::cerr << "Failed to accept connection\n";
                break;
            }
        }

        std::cout << "\nPhase 4 test complete. Accepted " << connection_count 
                  << " connections with request parsing.\n";
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
