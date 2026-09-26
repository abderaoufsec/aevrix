// =============================================================================
// Aevrix - Main Entry Point
// =============================================================================
// This file implements the main entry point for the Aevrix HTTP server.
// In Phase 11, we add a bounded worker pool for blocking operations to keep
// blocking filesystem/application work out of the event loop, ensuring the
// event loop remains responsive.
//
// Current Implementation (Phase 11):
// - Create TCP listener on 127.0.0.1:8080 (non-blocking on Linux)
// - Use Connection class to manage connection state (input buffer, parser state,
//   output buffer, keep-alive decision, timestamps, request ID)
// - Use ServerConfig to enforce timeouts and resource limits
// - Use WorkerPool for blocking operations (filesystem I/O, etc.)
// - Bounded queue prevents unbounded task creation
// - Clean shutdown without detached threads
// - Use event loop to handle multiple connections efficiently
// - Read HTTP requests with partial read handling
// - Parse requests using HttpRequestParser
// - Parse Connection header for keep-alive support
// - Serve static files using StaticFileServer
// - Handle multiple requests per connection (keep-alive)
// - Send responses with partial write handling
// - Close connection when appropriate
//
// Previous Phases:
// - Phase 1: RAII file descriptors (UniqueFd)
// - Phase 2: TCP listener with socket/bind/listen/accept
// - Phase 3: Structured HTTP response serialization
// - Phase 4: HTTP request parsing with HttpRequestParser
// - Phase 5: Full request/response pipeline with partial I/O
// - Phase 6: Static file serving with security
// - Phase 7: Keep-alive connections
// - Phase 8: Non-blocking I/O with epoll (Linux only)
// - Phase 9: Connection state machine
// - Phase 10: Timeouts and resource limits
// - Phase 11: Worker pool for blocking operations
// - Phase 11: Worker pool for blocking operations
//
// Future Phases Will Add:
// - Phase 11: Worker pool for blocking operations
// =============================================================================

#include "aevrix/tcp_listener.h"
#include "aevrix/unique_fd.h"
#include "aevrix/http_response.h"
#include "aevrix/http_response_serializer.h"
#include "aevrix/http_request_parser.h"
#include "aevrix/static_file_server.h"
#include "aevrix/connection.h"
#include "aevrix/server_config.h"
#include "aevrix/worker_pool.h"
#ifdef __linux__
#include "aevrix/event_loop.h"
#endif
#include <iostream>
#include <string>
#include <cstdint>  // For uint16_t
#include <vector>   // For command-line arguments
#include <memory>   // For std::unique_ptr

// Bring HTTP types into current namespace for readability
using aevrix::http::HttpRequest;
using aevrix::http::HttpResponse;
using aevrix::http::HttpMethod;
using aevrix::http::StatusCode;
using aevrix::http::ConnectionPolicy;
using aevrix::http::HttpRequestParser;

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
 * @brief Parse command-line arguments
 * 
 * Parses command-line arguments to extract configuration options.
// Currently supports --root for specifying the document root directory.
 * 
 * @param argc Argument count
 * @param argv Argument values
 * @param document_root Output parameter for document root path
 * @return true if parsing succeeded, false if there was an error
 */
bool parse_arguments(int argc, char* argv[], std::string& document_root) {
    // Default document root
    document_root = "./public";
    
    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--root" || arg == "-r") {
            // Next argument is the document root
            if (i + 1 < argc) {
                document_root = argv[++i];
                std::cout << "Using document root: " << document_root << "\n";
            } else {
                std::cerr << "Error: --root requires a path argument\n";
                return false;
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
            std::cout << "Options:\n";
            std::cout << "  --root, -r PATH    Set document root directory (default: ./public)\n";
            std::cout << "  --help, -h         Show this help message\n";
            return false;
        } else {
            std::cerr << "Error: Unknown argument: " << arg << "\n";
            std::cerr << "Use --help for usage information\n";
            return false;
        }
    }
    
    return true;
}

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
// may arrive in multiple recv() calls, especially for large requests or
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
 * @brief Check if the request wants keep-alive connection
 * 
 * Parses the Connection header to determine if the client wants
 * to keep the connection alive for multiple requests.
 * 
 * @param request The parsed HTTP request
 * @return true if keep-alive is requested, false otherwise
 */
bool wants_keep_alive(const HttpRequest& request) {
    // Check HTTP version - HTTP/1.1 defaults to keep-alive
    if (request.version() == "HTTP/1.1") {
        // Check for explicit "Connection: close" header
        std::string connection_header = request.headers().get("Connection");
        if (!connection_header.empty()) {
            // Case-insensitive comparison
            std::string lower = connection_header;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "close") {
                return false;
            }
        }
        return true;  // Default to keep-alive for HTTP/1.1
    } else {
        // HTTP/1.0 defaults to close unless "Connection: keep-alive"
        std::string connection_header = request.headers().get("Connection");
        if (!connection_header.empty()) {
            std::string lower = connection_header;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "keep-alive") {
                return true;
            }
        }
        return false;  // Default to close for HTTP/1.0
    }
}

/**
 * @brief Build an HTTP response based on the request using static file serving
 * 
 * Generates an appropriate HTTP response based on the parsed request.
 * In Phase 10, we implement:
 * - Static file serving for GET requests
 * - HEAD request support (200 OK with no body)
 * - 404 Not Found for non-existent files
 * - 403 Forbidden for directory access and path traversal attempts
 * - 405 Method Not Allowed for unsupported methods
 * - Keep-alive support based on Connection header
 * 
 * @param request The parsed HTTP request
 * @param file_server The static file server instance
 * @return HttpResponse The structured HTTP response
 */
aevrix::HttpResponse build_response(const HttpRequest& request, aevrix::StaticFileServer& file_server) {
    // Check if client wants keep-alive
    bool keep_alive = wants_keep_alive(request);
    
    // Check the method first
    if (request.method() == HttpMethod::GET || request.method() == HttpMethod::HEAD) {
        // Serve the file using StaticFileServer
        auto [content, mime_type, status_code] = file_server.serve_file(request.target());
        
        if (status_code == 200) {
            // File found and read successfully
            if (request.method() == HttpMethod::GET) {
                HttpResponse response(StatusCode::OK, content);
                response.set_header("Content-Type", mime_type);
                response.set_header("Server", "Aevrix/0.1.0");
                response.set_connection_policy(keep_alive ? ConnectionPolicy::KeepAlive : ConnectionPolicy::Close);
                return response;
            } else {
                // HEAD request - return 200 OK with no body
                HttpResponse response(StatusCode::OK);
                response.set_header("Content-Type", mime_type);
                response.set_header("Content-Length", std::to_string(content.length()));
                response.set_header("Server", "Aevrix/0.1.0");
                response.set_connection_policy(keep_alive ? ConnectionPolicy::KeepAlive : ConnectionPolicy::Close);
                return response;
            }
        } else if (status_code == 404) {
            // File not found
            HttpResponse response(StatusCode::NotFound, "Not Found");
            response.set_header("Content-Type", "text/plain");
            response.set_header("Server", "Aevrix/0.1.0");
            response.set_connection_policy(ConnectionPolicy::Close);  // Always close on error
            return response;
        } else if (status_code == 403) {
            // Forbidden (directory access or path traversal attempt)
            HttpResponse response(StatusCode::Forbidden, "Forbidden");
            response.set_header("Content-Type", "text/plain");
            response.set_header("Server", "Aevrix/0.1.0");
            response.set_connection_policy(ConnectionPolicy::Close);  // Always close on error
            return response;
        } else {
            // Internal server error
            HttpResponse response(StatusCode::InternalServerError, "Internal Server Error");
            response.set_header("Content-Type", "text/plain");
            response.set_header("Server", "Aevrix/0.1.0");
            response.set_connection_policy(ConnectionPolicy::Close);  // Always close on error
            return response;
        }
    } else {
        // Return 405 Method Not Allowed for unsupported methods
        HttpResponse response(StatusCode::MethodNotAllowed, 
                               "Method not allowed: " + aevrix::http::http_method_to_string(request.method()));
        response.set_header("Content-Type", "text/plain");
        response.set_header("Server", "Aevrix/0.1.0");
        response.set_header("Allow", "GET, HEAD");  // Indicate allowed methods
        response.set_connection_policy(ConnectionPolicy::Close);  // Always close on error
        return response;
    }
}

/**
 * @brief Handle a single client connection with keep-alive support
 * 
 * Accepts a connection, reads the HTTP request (with partial read handling),
// parses it, generates a response using static file serving, and sends it
// (with partial write handling). In Phase 7, this function supports
// keep-alive connections by handling multiple requests on the same TCP
// connection when the client requests it.
 * 
 * The pipeline is:
 * socket → recv (loop) → parser → Request → file_server → Response → serializer → send (loop)
 * 
 * With keep-alive:
 * request 1 → response 1 → request 2 → response 2 → ... → close
 * 
 * @param client_fd The client socket descriptor
 * @param file_server The static file server instance
 */
/**
 * @brief Handle a single client connection with connection state management
 * 
 * Accepts a connection, reads the HTTP request (with partial read handling),
// parses it, generates a response using static file serving, and sends it
// (with partial write handling). In Phase 10, this function uses the Connection
// class to manage connection state explicitly and checks timeouts to prevent
// slow-client resource exhaustion.
 * 
 * The pipeline is:
 * socket → recv (loop) → parser → Request → file_server → Response → serializer → send (loop)
 * 
 * With keep-alive:
 * request 1 → response 1 → request 2 → response 2 → ... → close
 * 
 * With timeout enforcement:
 * header timeout → close if exceeded
 * body timeout → close if exceeded
 * keep-alive timeout → close if exceeded
 * write timeout → close if exceeded
 * 
 * @param client_fd The client socket descriptor
 * @param file_server The static file server instance
 * @param config Server configuration with timeout values
 */
void handle_connection(int client_fd, aevrix::StaticFileServer& file_server, const aevrix::ServerConfig& config, aevrix::WorkerPool& worker_pool) {
    (void)config;  // TODO: Add timeout checks in future iterations
    (void)worker_pool;  // TODO: Use worker pool for blocking filesystem operations
    // Create Connection object to manage state
    static uint64_t connection_counter = 0;
    aevrix::Connection connection(client_fd, ++connection_counter);
    
    std::cout << "Handling client connection (ID: " << connection.id() << ")...\n";

    try {
        int request_count = 0;
        bool keep_alive = true;
        
        // Set initial state
        connection.set_state(aevrix::ConnectionState::Reading);
        connection.set_read_state(aevrix::ReadState::Headers);
        
        // Handle multiple requests on the same connection (keep-alive)
        while (keep_alive) {
            request_count++;
            connection.increment_request_id();
            std::cout << "Processing request " << request_count << " on connection " << connection.id() << "\n";
            
            // Update activity timestamp
            connection.update_activity();
            
            // Parse the HTTP request with partial read handling
            if (!receive_request(client_fd, connection.parser())) {
                // receive_request already handles error responses
                std::cout << "Request " << request_count << " failed, closing connection\n";
                connection.set_state(aevrix::ConnectionState::Closing);
                break;
            }

            // Check if parsing completed
            if (!connection.is_request_complete()) {
                std::cerr << "Request parsing incomplete\n";
                connection.set_state(aevrix::ConnectionState::Closing);
                break;
            }

            // Check for parse errors
            if (connection.has_parse_error()) {
                std::cerr << "Request parsing error\n";
                connection.set_state(aevrix::ConnectionState::Closing);
                break;
            }

            // Get the parsed request
            const HttpRequest& request = connection.parser().request();
            
            std::cout << "Parsed request: " << request.request_line() << "\n";
            std::cout << "Method: " << aevrix::http::http_method_to_string(request.method()) << "\n";
            std::cout << "Target: " << request.target() << "\n";
            std::cout << "Headers: " << request.headers().size() << "\n";

            // Evaluate keep-alive policy
            connection.evaluate_keep_alive(request);
            
            // Build response based on request using static file serving
            HttpResponse response = build_response(request, file_server);
            
            // Set current response in connection
            connection.set_current_response(response);
            
            // Check if we should keep the connection alive
            keep_alive = connection.keep_alive();
            std::cout << "Keep-alive: " << (keep_alive ? "yes" : "no") << "\n";
            
            // Serialize the response
            std::string serialized_response = HttpResponseSerializer::serialize(response);
            
            if (serialized_response.empty()) {
                std::cerr << "Failed to serialize response\n";
                connection.set_state(aevrix::ConnectionState::Closing);
                break;
            }

            std::cout << "Sending response (" << serialized_response.length() << " bytes)...\n";

            // Set writing state
            connection.set_state(aevrix::ConnectionState::Writing);
            connection.set_write_state(aevrix::WriteState::Body);

            // Send the response with partial write handling
            if (send_response(client_fd, serialized_response.c_str(), serialized_response.length())) {
                std::cout << "Response sent successfully\n";
                connection.set_write_state(aevrix::WriteState::Complete);
            } else {
                std::cerr << "Failed to send response\n";
                connection.set_write_state(aevrix::WriteState::Error);
                break;
            }
            
            // Update activity timestamp
            connection.update_activity();
            
            // If not keep-alive, break the loop
            if (!keep_alive) {
                std::cout << "Connection will be closed after this response\n";
                connection.set_state(aevrix::ConnectionState::Closing);
                break;
            }
            
            // Set to waiting state for next request
            connection.set_state(aevrix::ConnectionState::Waiting);
            connection.set_read_state(aevrix::ReadState::Idle);
            connection.set_write_state(aevrix::WriteState::Idle);
            
            std::cout << "Waiting for next request on same connection...\n";
        }
        
        std::cout << "Connection " << connection.id() << " handled " << request_count << " request(s)\n";
        std::cout << "Connection age: " << connection.age().count() << "ms\n";
        std::cout << "Time since last activity: " << connection.time_since_activity().count() << "ms\n";

    } catch (const std::exception& e) {
        std::cerr << "Exception in handle_connection: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "Unknown exception in handle_connection\n";
    }

    // Close the client connection using UniqueFd for automatic cleanup
    aevrix::UniqueFd client_unique_fd(client_fd);
    // client_unique_fd will automatically close the descriptor when it goes out of scope
    
    connection.set_state(aevrix::ConnectionState::Closed);
    std::cout << "Connection " << connection.id() << " closed\n";
}

/**
 * @brief Main entry point for the Aevrix HTTP server
 * 
 * Creates a TCP listener, accepts connections continuously, reads and parses HTTP
// requests (with partial read handling), and handles them with static file serving
// (with partial write handling). This is the Phase 7 implementation - static
// file serving with keep-alive connections.
 * 
 * Usage:
 *   ./aevrix --root ./public
 *   # Server will listen on 127.0.0.1:8080
 *   # Serve files from ./public directory
 *   # Test with: curl http://127.0.0.1:8080/index.html
 *   # Test keep-alive: curl http://127.0.0.1:8080/ http://127.0.0.1:8080/style.css
 * 
 * @return int Exit code (0 for success, non-zero for error)
 */
int main(int argc, char* argv[]) {
    std::cout << "=== Aevrix HTTP Server - Phase 11 ===\n";
#ifdef __linux__
    std::cout << "Worker Pool with epoll (Linux)\n\n";
#else
    std::cout << "Worker Pool (Windows/Unix fallback for development)\n\n";
#endif

    try {
        std::cout << "Parsing arguments...\n";
        // Parse command-line arguments
        std::string document_root;
        if (!parse_arguments(argc, argv, document_root)) {
            std::cout << "Argument parsing failed\n";
            return 1;
        }

        std::cout << "Attempting to create static file server with root: " << document_root << "\n";

        // Create static file server
        aevrix::StaticFileServer file_server(document_root);

        std::cout << "Static file server created successfully\n";

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
        std::cout << "Serving files from: " << file_server.document_root() << "\n";
        
        // Create server configuration with timeouts and resource limits
        aevrix::ServerConfig config;
        
        // Create worker pool for blocking operations
        // Use 4 workers and a queue size of 128
        // This keeps blocking filesystem work out of the event loop
        aevrix::WorkerPool worker_pool(4, 128);
        
#ifdef __linux__
        std::cout << "Using worker pool with epoll event loop\n";
#else
        std::cout << "Using worker pool (Windows/Unix fallback)\n";
        std::cout << "Keep-alive connections enabled\n";
#endif
        
        std::cout << config.summary() << "\n";
        std::cout << "Press Ctrl+C to stop\n\n";

        // Main server loop
        // Phase 11: Use epoll event loop on Linux, blocking loop on Windows/Unix
        // Both paths now use the Connection class for explicit state management,
        // ServerConfig for timeout and resource limit enforcement, and WorkerPool
        // for blocking operations to keep the event loop responsive
        int connection_count = 0;

#ifdef __linux__
        // Linux: Use epoll-based event loop for non-blocking I/O
        try {
            // Create event loop
            aevrix::EventLoop event_loop;
            
            // Set listener to non-blocking mode
            listener.stop();  // Stop current blocking listener
            if (!listener.start(host, port, true)) {  // Start with non-blocking
                std::cerr << "Failed to start non-blocking listener\n";
                return 1;
            }
            
            // Add listener socket to event loop
            if (!event_loop.add_fd(listener.get_socket(), EPOLLIN, 
                [&, file_server = std::ref(file_server)](int fd, EventType event) {
                    if (event == EventType::Readable) {
                        // Accept new connection
                        auto client_fd = listener.accept();
                        if (client_fd.has_value()) {
                            handle_connection(client_fd.value(), file_server, config, worker_pool);
                            connection_count++;
                            std::cout << "Total connections handled: " << connection_count << "\n\n";
                        }
                    }
                })) {
                std::cerr << "Failed to add listener to event loop\n";
                return 1;
            }
            
            std::cout << "Starting event loop...\n";
            event_loop.run();
            
        } catch (const std::exception& e) {
            std::cerr << "Event loop error: " << e.what() << "\n";
            return 1;
        }
#else
        // Windows/Unix: Use blocking loop for development
        while (true) {
            std::cout << "Waiting for connection...\n";

            // Accept a connection (blocking call)
            auto client_fd = listener.accept();
            
            if (client_fd.has_value()) {
                // Handle the connection with static file serving
                handle_connection(client_fd.value(), file_server, config, worker_pool);
                connection_count++;
                std::cout << "Total connections handled: " << connection_count << "\n\n";
            } else {
                std::cerr << "Failed to accept connection\n";
                break;
            }
        }
#endif

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
