// =============================================================================
// Aevrix - Main Entry Point
// =============================================================================
// This file implements the main entry point for the Aevrix HTTP server.
// In Phase 5, we implement the full request/response pipeline with:
// - Partial reads (handling requests arriving in multiple recv() calls)
// - Partial writes (handling responses requiring multiple send() calls)
// - Proper error responses (404 for non-existent paths)
// - Continuous server operation (no connection limit)
//
// Current Implementation (Phase 5):
// - Create TCP listener on 127.0.0.1:8080
// - Accept incoming connections continuously
// - Read HTTP requests with partial read handling
// - Parse requests using HttpRequestParser
// - Generate structured HTTP responses (including 404 for unknown paths)
// - Send responses with partial write handling
// - Close the connection
//
// Previous Phases:
// - Phase 1: RAII file descriptors (UniqueFd)
// - Phase 2: TCP listener with socket/bind/listen/accept
// - Phase 3: Structured HTTP response serialization
// - Phase 4: HTTP request parsing with HttpRequestParser
//
// Future Phases Will Add:
// - Phase 6: Static file serving
// - Phase 7: Keep-alive connections
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
 * @brief Send data to a client socket with partial write handling
 * 
 * Sends the given data to the specified client socket descriptor.
 * In Phase 5, this function handles partial writes by looping until all data
 * is sent. This is necessary because TCP send() may not send all data in a
 * single call, especially for large responses or due to network conditions.
 * 
 * @param client_fd The client socket descriptor
 * @param data The data to send
 * @param length The length of the data
 * @return true if all data was sent successfully, false on error
 */
bool send_response(int client_fd, const char* data, size_t length) {
    size_t total_sent = 0;
    
    // Loop to handle partial writes
    // TCP send() may not send all data in a single call
    while (total_sent < length) {
        size_t remaining = length - total_sent;
        
#ifdef _WIN32
        SOCKET sock = static_cast<SOCKET>(client_fd);
        int sent = send(sock, data + total_sent, static_cast<int>(remaining), 0);
        
        if (sent == SOCKET_ERROR) {
            std::cerr << "send() failed: " << WSAGetLastError() << "\n";
            return false;
        }
#else
        ssize_t sent = send(client_fd, data + total_sent, remaining, 0);
        
        if (sent < 0) {
            std::cerr << "send() failed: " << strerror(errno) << "\n";
            return false;
        }
#endif
        
        total_sent += sent;
        std::cout << "Sent " << sent << " bytes (" << total_sent << "/" << length << " total)\n";
    }
    
    return true;
}

/**
 * @brief Send an error response to the client
 * 
 * Sends a standardized error response when request parsing or I/O fails.
 * 
 * @param client_fd The client socket descriptor
 * @param status The HTTP status code for the error
 * @param message The error message
 */
void send_error_response(int client_fd, StatusCode status, const std::string& message) {
    HttpResponse error_response(status, message);
    error_response.set_header("Content-Type", "text/plain");
    error_response.set_header("Server", "Aevrix/0.1.0");
    error_response.set_connection_policy(ConnectionPolicy::Close);
    
    std::string serialized = HttpResponseSerializer::serialize(error_response);
    send_response(client_fd, serialized.c_str(), serialized.length());
}

/**
 * @brief Receive data from a client socket with partial read handling
 * 
 * Receives data from the specified client socket descriptor.
 * In Phase 5, this function handles partial reads by looping until either:
 * - The parser indicates the request is complete
 * - The connection is closed
 * - An error occurs
 * 
 * This is necessary because TCP is a stream protocol - a single HTTP request
 * may arrive in multiple recv() calls, especially for large requests or
// due to network conditions.
 * 
 * @param client_fd The client socket descriptor
 * @param parser The HttpRequestParser to feed data to
 * @return true if request was parsed successfully, false on error
 */
bool receive_request(int client_fd, HttpRequestParser& parser) {
    constexpr size_t BUFFER_SIZE = 8192;
    char buffer[BUFFER_SIZE];
    
    // Loop to handle partial reads
    // HTTP requests may arrive in multiple TCP packets
    while (!parser.is_complete() && !parser.has_error()) {
#ifdef _WIN32
        SOCKET sock = static_cast<SOCKET>(client_fd);
        int received = recv(sock, buffer, static_cast<int>(BUFFER_SIZE), 0);
        
        if (received == SOCKET_ERROR) {
            std::cerr << "recv() failed: " << WSAGetLastError() << "\n";
            // Send 400 Bad Request for recv errors
            send_error_response(client_fd, StatusCode::BadRequest, "Receive error");
            return false;
        }
        
        if (received == 0) {
            // Connection closed by client
            std::cerr << "Connection closed by client\n";
            return false;
        }
#else
        ssize_t received = recv(client_fd, buffer, BUFFER_SIZE, 0);
        
        if (received < 0) {
            std::cerr << "recv() failed: " << strerror(errno) << "\n";
            // Send 400 Bad Request for recv errors
            send_error_response(client_fd, StatusCode::BadRequest, "Receive error");
            return false;
        }
        
        if (received == 0) {
            // Connection closed by client
            std::cerr << "Connection closed by client\n";
            return false;
        }
#endif
        
        std::cout << "Received " << received << " bytes from client\n";
        
        // Feed the received data to the parser
        parser.feed(buffer, received);
        
        // Check if we've hit configured limits
        if (parser.has_error()) {
            std::cerr << "Parser error: " << parser.error_message() << "\n";
            // Send 400 Bad Request for parsing errors
            send_error_response(client_fd, StatusCode::BadRequest, parser.error_message());
            return false;
        }
    }
    
    return parser.is_complete();
}

/**
 * @brief Build an HTTP response based on the request
 * 
 * Generates an appropriate HTTP response based on the parsed request.
 * In Phase 5, we implement:
 * - 200 OK for GET requests to "/"
 * - 200 OK with no body for HEAD requests to "/"
 * - 404 Not Found for any other path
 * - 405 Method Not Allowed for unsupported methods
 * 
 * Note: Phase 6 will implement static file serving, which will replace
// the simple path-based logic with actual file system operations.
 * 
 * @param request The parsed HTTP request
 * @return HttpResponse The structured HTTP response
 */
HttpResponse build_response(const HttpRequest& request) {
    // Check the method first
    if (request.method() == HttpMethod::GET || request.method() == HttpMethod::HEAD) {
        // In Phase 5, we only support the root path "/"
        // Phase 6 will implement static file serving for arbitrary paths
        if (request.target() == "/") {
            // Return 200 OK for root path
            if (request.method() == HttpMethod::GET) {
                HttpResponse response(StatusCode::OK, "Hello from Aevrix!");
                response.set_header("Content-Type", "text/plain");
                response.set_header("Server", "Aevrix/0.1.0");
                response.set_connection_policy(ConnectionPolicy::Close);
                return response;
            } else {
                // HEAD request - return 200 OK with no body
                HttpResponse response(StatusCode::OK);
                response.set_header("Content-Type", "text/plain");
                response.set_header("Content-Length", "0");  // HEAD responses have no body
                response.set_header("Server", "Aevrix/0.1.0");
                response.set_connection_policy(ConnectionPolicy::Close);
                return response;
            }
        } else {
            // Return 404 Not Found for non-root paths
            // Phase 6 will implement actual file serving
            HttpResponse response(StatusCode::NotFound, "Not Found");
            response.set_header("Content-Type", "text/plain");
            response.set_header("Server", "Aevrix/0.1.0");
            response.set_connection_policy(ConnectionPolicy::Close);
            return response;
        }
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
 * Accepts a connection, reads the HTTP request (with partial read handling),
 * parses it, generates a response, and sends it (with partial write handling).
 * This is the Phase 5 implementation with proper I/O handling.
 * 
 * The pipeline is:
 * socket → recv (loop) → parser → Request → handler → Response → serializer → send (loop)
 * 
 * @param client_fd The client socket descriptor
 */
void handle_connection(int client_fd) {
    std::cout << "Handling client connection...\n";

    try {
        // Parse the HTTP request with partial read handling
        HttpRequestParser parser;
        
        if (!receive_request(client_fd, parser)) {
            // receive_request already handles error responses
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

        // Send the response with partial write handling
        if (send_response(client_fd, serialized_response.c_str(), serialized_response.length())) {
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
 * Creates a TCP listener, accepts connections continuously, reads and parses HTTP
// requests (with partial read handling), and handles them with structured HTTP
// responses (with partial write handling). This is the Phase 5 implementation -
// full request/response pipeline with proper I/O handling.
 * 
 * Usage:
 *   ./aevrix
 *   # Server will listen on 127.0.0.1:8080
 *   # Test with: curl http://127.0.0.1:8080/
 *   # Test 404 with: curl http://127.0.0.1:8080/does-not-exist
 * 
 * @return int Exit code (0 for success, non-zero for error)
 */
int main() {
    std::cout << "=== Aevrix HTTP Server - Phase 5 ===\n";
    std::cout << "Full Request/Response Pipeline\n\n";

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
        std::cout << "Full request/response pipeline with partial I/O handling\n";
        std::cout << "Press Ctrl+C to stop\n\n";

        // Main server loop
        // In Phase 5, this runs continuously (no connection limit)
        // Phase 8 will replace this with epoll-based event loop
        int connection_count = 0;

        while (true) {
            std::cout << "Waiting for connection...\n";

            // Accept a connection (blocking call)
            auto client_fd = listener.accept();
            
            if (client_fd.has_value()) {
                // Handle the connection with full request/response pipeline
                handle_connection(client_fd.value());
                connection_count++;
                std::cout << "Total connections handled: " << connection_count << "\n\n";
            } else {
                std::cerr << "Failed to accept connection\n";
                break;
            }
        }

        std::cout << "\nServer stopping...\n";

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
