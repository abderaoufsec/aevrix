// =============================================================================
// Aevrix - Main Entry Point
// =============================================================================
// This file implements the main entry point for the Aevrix HTTP server.
// In Phase 15, we add graceful shutdown to make shutdown safe and observable.
// The shutdown sequence is: stop accepting → finish safe work → close connections
// → stop workers → flush logs → exit. SIGINT and SIGTERM (Linux) and Ctrl+C
// (Windows) trigger graceful shutdown without corrupting internal state.
//
// Current Implementation (Phase 13):
// - Use configuration system to load settings from file or command-line
// - Create TCP listener on configured host:port (non-blocking on Linux)
// - Use Connection class to manage connection state (input buffer, parser state,
//   output buffer, keep-alive decision, timestamps, request ID)
// - Use ServerConfig to enforce timeouts and resource limits (from config file)
// - Use WorkerPool for blocking operations (filesystem I/O, etc.)
// - Use Router for application-level routing (GET /, GET /health, GET /metrics)
// - Bounded queue prevents unbounded task creation
// - Clean shutdown without detached threads
// - Router consumes Request and produces Response (no socket knowledge)
// - Use event loop to handle multiple connections efficiently
// - Read HTTP requests with partial read handling
// - Parse requests using HttpRequestParser
// - Parse Connection header for keep-alive support
// - Route requests to handlers (or fall back to static file serving)
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
// - Phase 12: Router for application-level routing
// - Phase 13: Configuration system
// - Phase 14: Structured logging
// - Phase 15: Graceful shutdown
//
// Future Phases Will Add:
// - Phase 13: Configuration system
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
#include "aevrix/router.h"
#include "aevrix/config_parser.h"
#include "aevrix/logger.h"
#include "aevrix/signal_handler.h"
#ifdef __linux__
#include "aevrix/event_loop.h"
#endif
#include <iostream>
#include <string>
#include <cstdint>  // For uint16_t
#include <vector>   // For command-line arguments
#include <memory>   // For std::unique_ptr
#include <chrono>   // For timing

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
// In Phase 13, supports --config for configuration file and --root for document root.
 * 
 * @param argc Argument count
 * @param argv Argument values
 * @param config_file Output parameter for configuration file path
 * @param document_root Output parameter for document root path
 * @return true if parsing succeeded, false if there was an error
 */
bool parse_arguments(int argc, char* argv[], std::string& config_file, std::string& document_root) {
    // Default values
    config_file = "";
    document_root = "./public";
    
    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--config" || arg == "-c") {
            // Next argument is the configuration file
            if (i + 1 < argc) {
                config_file = argv[++i];
                aevrix::g_logger.info("Using configuration file: " + config_file);
            } else {
                aevrix::g_logger.error("--config requires a file argument");
                return false;
            }
        } else if (arg == "--root" || arg == "-r") {
            // Next argument is the document root
            if (i + 1 < argc) {
                document_root = argv[++i];
                aevrix::g_logger.info("Using document root: " + document_root);
            } else {
                aevrix::g_logger.error("--root requires a path argument");
                return false;
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
            std::cout << "Options:\n";
            std::cout << "  --config, -c FILE   Load configuration from file\n";
            std::cout << "  --root, -r PATH     Set document root directory (default: ./public)\n";
            std::cout << "  --help, -h          Show this help message\n";
            return false;
        } else {
            aevrix::g_logger.error("Unknown argument: " + arg);
            std::cout << "Use --help for usage information\n";
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
            aevrix::g_logger.error("send() failed: " + std::to_string(WSAGetLastError()));
            return false;
        }
#else
        ssize_t sent = send(client_fd, data + total_sent, remaining, 0);
        
        if (sent < 0) {
            aevrix::g_logger.error("send() failed: " + std::string(strerror(errno)));
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
            aevrix::g_logger.error("recv() failed: " + std::to_string(WSAGetLastError()));
            // Send 400 Bad Request for recv errors
            send_error_response(client_fd, StatusCode::BadRequest, "Receive error");
            return false;
        }
        
        if (received == 0) {
            // Connection closed by client
            aevrix::g_logger.warn("Connection closed by client");
            return false;
        }
#else
        ssize_t received = recv(client_fd, buffer, BUFFER_SIZE, 0);
        
        if (received < 0) {
            aevrix::g_logger.error("recv() failed: " + std::string(strerror(errno)));
            // Send 400 Bad Request for recv errors
            send_error_response(client_fd, StatusCode::BadRequest, "Receive error");
            return false;
        }
        
        if (received == 0) {
            // Connection closed by client
            aevrix::g_logger.warn("Connection closed by client");
            return false;
        }
#endif
        
        std::cout << "Received " << received << " bytes from client\n";
        
        // Feed the received data to the parser
        parser.feed(buffer, received);
        
        // Check if we've hit configured limits
        if (parser.has_error()) {
            aevrix::g_logger.error("Parser error: " + parser.error_message());
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
aevrix::HttpResponse build_response(const HttpRequest& request, aevrix::StaticFileServer& file_server, aevrix::Router& router) {
    // First, try to route the request through the router
    // The router consumes Request and produces Response (no socket knowledge)
    try {
        HttpResponse response = router.route(request);
        // Router found a handler, return the response
        return response;
    } catch (const std::exception& e) {
        // Router returned an error, fall back to static file serving
        aevrix::g_logger.warn("Router error: " + std::string(e.what()) + ", falling back to static file serving");
    }
    
    // No route found or router error, fall back to static file serving
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
void handle_connection(int client_fd, aevrix::StaticFileServer& file_server, const aevrix::ServerConfig& config, aevrix::WorkerPool& worker_pool, aevrix::Router& router) {
    (void)config;  // TODO: Add timeout checks in future iterations
    (void)worker_pool;  // TODO: Use worker pool for blocking filesystem operations
    // Create Connection object to manage state
    static uint64_t connection_counter = 0;
    aevrix::Connection connection(client_fd, ++connection_counter);
    
    aevrix::g_logger.log_with_connection(aevrix::LogLevel::INFO, connection.id(), 
                                         "Handling client connection");

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
            aevrix::g_logger.log_with_request(aevrix::LogLevel::INFO, connection.id(), connection.request_id(),
                                             "Processing request " + std::to_string(request_count));
            
            // Track request start time for access logging
            auto request_start = std::chrono::high_resolution_clock::now();
            
            // Update activity timestamp
            connection.update_activity();
            
            // Parse the HTTP request with partial read handling
            if (!receive_request(client_fd, connection.parser())) {
                // receive_request already handles error responses
                aevrix::g_logger.log_with_request(aevrix::LogLevel::WARN, connection.id(), connection.request_id(),
                                                 "Request failed, closing connection");
                connection.set_state(aevrix::ConnectionState::Closing);
                break;
            }

            // Check if parsing completed
            if (!connection.is_request_complete()) {
                aevrix::g_logger.log_with_request(aevrix::LogLevel::ERR, connection.id(), connection.request_id(),
                                                 "Request parsing incomplete");
                connection.set_state(aevrix::ConnectionState::Closing);
                break;
            }

            // Check for parse errors
            if (connection.has_parse_error()) {
                aevrix::g_logger.log_with_request(aevrix::LogLevel::ERR, connection.id(), connection.request_id(),
                                                 "Request parsing error");
                connection.set_state(aevrix::ConnectionState::Closing);
                break;
            }

            // Get the parsed request
            const HttpRequest& request = connection.parser().request();
            
            aevrix::g_logger.log_with_request(aevrix::LogLevel::DEBUG, connection.id(), connection.request_id(),
                                             "Parsed request: " + request.request_line());
            aevrix::g_logger.log_with_request(aevrix::LogLevel::DEBUG, connection.id(), connection.request_id(),
                                             "Method: " + aevrix::http::http_method_to_string(request.method()));
            aevrix::g_logger.log_with_request(aevrix::LogLevel::DEBUG, connection.id(), connection.request_id(),
                                             "Target: " + request.target());
            aevrix::g_logger.log_with_request(aevrix::LogLevel::DEBUG, connection.id(), connection.request_id(),
                                             "Headers: " + std::to_string(request.headers().size()));

            // Evaluate keep-alive policy
            connection.evaluate_keep_alive(request);
            
            // Build response based on request using router or static file serving
            HttpResponse response = build_response(request, file_server, router);
            
            // Set current response in connection
            connection.set_current_response(response);
            
            // Check if we should keep the connection alive
            keep_alive = connection.keep_alive();
            aevrix::g_logger.log_with_request(aevrix::LogLevel::DEBUG, connection.id(), connection.request_id(),
                                             "Keep-alive: " + std::string(keep_alive ? "yes" : "no"));
            
            // Serialize the response
            std::string serialized_response = HttpResponseSerializer::serialize(response);
            
            if (serialized_response.empty()) {
                aevrix::g_logger.log_with_request(aevrix::LogLevel::ERR, connection.id(), connection.request_id(),
                                                 "Failed to serialize response");
                connection.set_state(aevrix::ConnectionState::Closing);
                break;
            }

            aevrix::g_logger.log_with_request(aevrix::LogLevel::DEBUG, connection.id(), connection.request_id(),
                                             "Sending response (" + std::to_string(serialized_response.length()) + " bytes)");

            // Set writing state
            connection.set_state(aevrix::ConnectionState::Writing);
            connection.set_write_state(aevrix::WriteState::Body);

            // Send the response with partial write handling
            if (send_response(client_fd, serialized_response.c_str(), serialized_response.length())) {
                aevrix::g_logger.log_with_request(aevrix::LogLevel::DEBUG, connection.id(), connection.request_id(),
                                                 "Response sent successfully");
                connection.set_write_state(aevrix::WriteState::Complete);
                
                // Log access with timing
                auto request_end = std::chrono::high_resolution_clock::now();
                auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(request_end - request_start).count();
                aevrix::g_logger.access(connection.id(), connection.request_id(),
                                       aevrix::http::http_method_to_string(request.method()),
                                       request.target(),
                                       static_cast<int>(response.status()),
                                       response.body().size(),
                                       static_cast<uint64_t>(duration_us));
            } else {
                aevrix::g_logger.log_with_request(aevrix::LogLevel::ERR, connection.id(), connection.request_id(),
                                                 "Failed to send response");
                connection.set_write_state(aevrix::WriteState::Error);
                break;
            }
            
            // Update activity timestamp
            connection.update_activity();
            
            // If not keep-alive, break the loop
            if (!keep_alive) {
                aevrix::g_logger.log_with_request(aevrix::LogLevel::DEBUG, connection.id(), connection.request_id(),
                                                 "Connection will be closed after this response");
                connection.set_state(aevrix::ConnectionState::Closing);
                break;
            }
            
            // Set to waiting state for next request
            connection.set_state(aevrix::ConnectionState::Waiting);
            connection.set_read_state(aevrix::ReadState::Idle);
            connection.set_write_state(aevrix::WriteState::Idle);
            
            aevrix::g_logger.log_with_request(aevrix::LogLevel::DEBUG, connection.id(), connection.request_id(),
                                             "Waiting for next request on same connection");
        }
        
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::INFO, connection.id(),
                                           "Handled " + std::to_string(request_count) + " request(s)");
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, connection.id(),
                                           "Connection age: " + std::to_string(connection.age().count()) + "ms");
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, connection.id(),
                                           "Time since last activity: " + std::to_string(connection.time_since_activity().count()) + "ms");

    } catch (const std::exception& e) {
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::ERR, connection.id(),
                                           "Exception in handle_connection: " + std::string(e.what()));
    } catch (...) {
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::ERR, connection.id(),
                                           "Unknown exception in handle_connection");
    }

    // Close the client connection using UniqueFd for automatic cleanup
    aevrix::UniqueFd client_unique_fd(client_fd);
    // client_unique_fd will automatically close the descriptor when it goes out of scope
    
    connection.set_state(aevrix::ConnectionState::Closed);
    aevrix::g_logger.log_with_connection(aevrix::LogLevel::INFO, connection.id(), "Connection closed");
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
    aevrix::g_logger.info("=== Aevrix HTTP Server - Phase 15 ===");
#ifdef __linux__
    aevrix::g_logger.info("Graceful Shutdown with epoll (Linux)");
#else
    aevrix::g_logger.info("Graceful Shutdown (Windows/Unix fallback for development)");
#endif

#ifdef _WIN32
    // Initialize Winsock on Windows
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        aevrix::g_logger.error("WSAStartup failed: " + std::to_string(result));
        return 1;
    }
#endif

    try {
        aevrix::g_logger.info("Parsing arguments...");
        // Parse command-line arguments
        std::string config_file;
        std::string document_root;
        if (!parse_arguments(argc, argv, config_file, document_root)) {
            aevrix::g_logger.error("Argument parsing failed");
#ifdef _WIN32
            WSACleanup();
#endif
            return 1;
        }

        // Set up signal handler for graceful shutdown
        aevrix::g_logger.info("Setting up signal handler for graceful shutdown");
        aevrix::g_signal_handler.set_shutdown_callback([]() {
            aevrix::g_logger.info("Shutdown callback triggered");
        });

        // Create server configuration
        aevrix::ServerConfig config;
        
        // Load configuration from file if specified
        if (!config_file.empty()) {
            aevrix::g_logger.info("Loading configuration from file: " + config_file);
            aevrix::ConfigParser parser;
            parser.parse_file(config_file);
            config.load_from_parser(parser);
        }
        
        // Override document root from command-line if specified
        if (!document_root.empty() && document_root != "./public") {
            config.set_document_root(document_root);
        }

        aevrix::g_logger.info("Attempting to create static file server with root: " + config.document_root());

        // Create static file server
        aevrix::StaticFileServer file_server(config.document_root());

        aevrix::g_logger.info("Static file server created successfully");

        // Create TCP listener on configured host:port
        aevrix::TcpListener listener(config.host(), config.port());
        
        if (!listener.is_listening()) {
            aevrix::g_logger.error("Failed to start TCP listener: " + listener.error_message());
            return 1;
        }

        aevrix::g_logger.info("Server running on http://" + listener.host() + ":" + std::to_string(listener.port()) + "/");
        aevrix::g_logger.info("Serving files from: " + file_server.document_root());
        
        // Create worker pool for blocking operations
        // Use configured number of workers and queue size
        // This keeps blocking filesystem work out of the event loop
        aevrix::WorkerPool worker_pool(config.workers(), 128);
        
        // Create router for application-level routing
        aevrix::Router router;
        
        // Register GET / route (root endpoint)
        router.add_route("GET", "/", [](const HttpRequest& request) {
            (void)request;  // Root endpoint doesn't need request details
            HttpResponse response(StatusCode::OK, "Aevrix HTTP Server v0.1.0\n");
            response.set_header("Content-Type", "text/plain");
            response.set_header("Server", "Aevrix/0.1.0");
            response.set_connection_policy(ConnectionPolicy::KeepAlive);
            return response;
        });
        
        // Register GET /health route (health check endpoint)
        router.add_route("GET", "/health", [](const HttpRequest& request) {
            (void)request;  // Health check doesn't need request details
            HttpResponse response(StatusCode::OK, "OK\n");
            response.set_header("Content-Type", "text/plain");
            response.set_header("Server", "Aevrix/0.1.0");
            response.set_connection_policy(ConnectionPolicy::KeepAlive);
            return response;
        });
        
        // Register GET /metrics route (metrics endpoint)
        router.add_route("GET", "/metrics", [](const HttpRequest& request) {
            (void)request;  // Metrics endpoint doesn't need request details yet
            HttpResponse response(StatusCode::OK, "Metrics endpoint - not yet implemented\n");
            response.set_header("Content-Type", "text/plain");
            response.set_header("Server", "Aevrix/0.1.0");
            response.set_connection_policy(ConnectionPolicy::KeepAlive);
            return response;
        });
        
#ifdef __linux__
        aevrix::g_logger.info("Using configuration system with epoll event loop");
#else
        aevrix::g_logger.info("Using configuration system (Windows/Unix fallback)");
        aevrix::g_logger.info("Keep-alive connections enabled");
#endif
        
        aevrix::g_logger.info(config.summary());
        aevrix::g_logger.info("Press Ctrl+C to stop");

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
                aevrix::g_logger.error("Failed to start non-blocking listener");
                return 1;
            }
            
            // Add listener socket to event loop
            if (!event_loop.add_fd(listener.get_socket(), EPOLLIN, 
                [&, file_server = std::ref(file_server)](int fd, EventType event) {
                    if (event == EventType::Readable) {
                        // Accept new connection
                        auto client_fd = listener.accept();
                        if (client_fd.has_value()) {
                            handle_connection(client_fd.value(), file_server, config, worker_pool, router);
                            connection_count++;
                            aevrix::g_logger.info("Total connections handled: " + std::to_string(connection_count));
                        }
                    }
                })) {
                aevrix::g_logger.error("Failed to add listener to event loop");
                return 1;
            }
            
            aevrix::g_logger.info("Starting event loop...");
            while (!aevrix::g_signal_handler.shutdown_requested()) {
                if (!event_loop.run(1000)) {  // 1 second timeout for shutdown check
                    break;
                }
            }
            
        } catch (const std::exception& e) {
            aevrix::g_logger.error("Event loop error: " + std::string(e.what()));
            return 1;
        }
#else
        // Windows/Unix: Use blocking loop for development
        aevrix::g_logger.info("Using blocking loop for development");
        while (!aevrix::g_signal_handler.shutdown_requested()) {
            aevrix::g_logger.info("Waiting for connection...");

            // Accept a connection (blocking call)
            auto client_fd = listener.accept();
            
            if (client_fd.has_value()) {
                // Handle the connection with static file serving
                handle_connection(client_fd.value(), file_server, config, worker_pool, router);
                connection_count++;
                aevrix::g_logger.info("Total connections handled: " + std::to_string(connection_count));
            } else {
                aevrix::g_logger.error("Failed to accept connection");
                break;
            }
        }
#endif

        aevrix::g_logger.info("Shutdown requested, stopping server...");

        // Graceful shutdown sequence:
        // 1. Stop accepting connections
        // 2. Finish safe work (worker pool shutdown)
        // 3. Close connections
        // 4. Stop workers
        // 5. Flush logs
        // 6. Exit
        aevrix::g_logger.info("Step 1: Stop accepting connections");
        listener.stop();

        aevrix::g_logger.info("Step 2: Finish safe work (worker pool shutdown)");
        worker_pool.shutdown();

        aevrix::g_logger.info("Step 3: Flush logs");
        std::cout.flush();

        aevrix::g_logger.info("Server stopped gracefully");

#ifdef _WIN32
        WSACleanup();
#endif
        return 0;

    } catch (const std::exception& e) {
        aevrix::g_logger.error("Exception: " + std::string(e.what()));
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    } catch (...) {
        aevrix::g_logger.error("Unknown exception occurred");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
}
