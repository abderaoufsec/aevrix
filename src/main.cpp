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
#include "aevrix/connection_manager.h"
#include "aevrix/server_config.h"
#include "aevrix/worker_pool.h"
#include "aevrix/router.h"
#include "aevrix/config_parser.h"
#include "aevrix/logger.h"
#include "aevrix/signal_handler.h"
#include "aevrix/worker_task.h"
#include "aevrix/worker_completion_handler.h"
#include "aevrix/filesystem_worker.h"
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
#include <string.h>  // For strerror on Linux
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
        
        total_sent += static_cast<size_t>(sent);
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
        parser.feed(buffer, static_cast<size_t>(received));
        
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
aevrix::HttpResponse build_response(const HttpRequest& request, aevrix::StaticFileServer& file_server, [[maybe_unused]] aevrix::Router& router) {
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
 * @brief Generate response for a request (Stage 4 - Response/Output State Machine)
 * 
 * This function generates an HTTP response for the given request.
 * It handles routing, static file serving, and error responses.
 * 
 * @param request The parsed HTTP request
 * @param file_server The static file server instance
 * @param router The router for application-level routing
 * @return The generated HTTP response
 */
aevrix::http::HttpResponse generate_response(const aevrix::http::HttpRequest& request,
                                               [[maybe_unused]] aevrix::Router& router) {
    // Stage 5: Only handle router responses here
    // Static file serving is now handled by WorkerPool
    try {
        aevrix::http::HttpResponse response = router.route(request);
        // Router returns response directly, not optional
        return response;
    } catch (const std::exception& e) {
        // Router returned an error
        aevrix::g_logger.warn("Router error: " + std::string(e.what()));
        // Return 500 error
        aevrix::http::HttpResponse response(aevrix::http::StatusCode::InternalServerError, 
                               "Internal Server Error");
        response.set_header("Content-Type", "text/plain");
        response.set_header("Server", "Aevrix/0.1.0");
        response.set_connection_policy(aevrix::http::ConnectionPolicy::Close);
        return response;
    }
}

/**
 * @brief Handle incremental write event for a connection (Stage 4 - Response/Output State Machine)
 * 
 * This function is called from the event loop when EPOLLOUT is set for a connection.
 * It performs nonblocking writes, handling partial writes and EAGAIN/EWOULDBLOCK.
 * 
 * The incremental write flow:
 * 1. Write available data (nonblocking, handles EAGAIN)
 * 2. Update write offset
 * 3. Check if output is complete
 * 4. If complete and keep-alive: disable EPOLLOUT, prepare for next request
 * 5. If complete and close: remove connection
 * 6. If EAGAIN: wait for next EPOLLOUT
 * 
 * @param conn The connection object
 * @param event_loop The event loop for modifying interest events
 * @return true if connection should remain open, false if it should close
 */
#ifdef __linux__
bool handle_write_event(std::shared_ptr<aevrix::Connection> conn, 
                        aevrix::EventLoop& event_loop) {
    aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, conn->id(), 
                                         "handle_write_event called");
    
    // Perform nonblocking write
    auto write_result = conn->write_nonblocking();
    
    if (write_result == aevrix::Connection::IoResult::InProgress) {
        // Send buffer full, wait for next EPOLLOUT
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, conn->id(), 
                                             "Send buffer full, waiting for EPOLLOUT");
        return true;  // Keep connection alive, wait for EPOLLOUT
    }
    
    if (write_result == aevrix::Connection::IoResult::Error) {
        // Socket error
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::ERR, conn->id(), 
                                             "Socket error during write");
        return false;  // Close connection
    }
    
    // Check if output is complete
    if (conn->is_output_complete()) {
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, conn->id(), 
                                             "Output complete");
        
        // Output complete - check keep-alive
        if (conn->keep_alive()) {
            // Keep connection alive for next request
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, conn->id(), 
                                                 "Keep-alive: preparing for next request");
            
            // Disable EPOLLOUT (no more output to write)
            event_loop.modify_fd(conn->fd(), EPOLLIN);
            
            // Reset output buffer for next response
            conn->clear_output_buffer();
            
            // Reset parser for next request
            conn->reset_parser();
            
            return true;  // Keep connection alive
        } else {
            // Close connection
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::INFO, conn->id(), 
                                                 "Connection close requested");
            return false;  // Close connection
        }
    }
    
    // Output not yet complete, wait for more EPOLLOUT
    aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, conn->id(),
                                         "Output incomplete, waiting for EPOLLOUT");
    return true;  // Keep connection alive, wait for EPOLLOUT
}
#endif

/**
 * @brief Handle incremental read event for a connection (Stage 3/4 - HTTP Incremental State Machine)
 * 
 * This function is called from the event loop when EPOLLIN is set for a connection.
 * It performs nonblocking reads, feeds data to the parser incrementally, and handles
 * parser state transitions. When a request is complete, it generates a response
 * and sets up the output state machine (Stage 4).
 * 
 * The incremental read flow:
 * 1. Read available data (nonblocking, handles EAGAIN)
 * 2. Append to connection's input buffer
 * 3. Feed input buffer to parser
 * 4. Clear input buffer after feeding
 * 5. Check parser state:
 *    - Incomplete → wait for more EPOLLIN data
 *    - Complete → generate response (Stage 4)
 *    - Error → send error response and close
 *    - Oversized → send 413 error and close
 * 
 * @param conn The connection object
 * @param file_server The static file server instance
 * @param router The router for application-level routing
 * @param event_loop The event loop for modifying interest events
 * @return true if connection should remain open, false if it should close
 */
#ifdef __linux__
bool handle_read_event(std::shared_ptr<aevrix::Connection> conn, 
                       [[maybe_unused]] aevrix::Router& router,
                       aevrix::EventLoop& event_loop,
                       aevrix::WorkerPool& worker_pool,
                       aevrix::WorkerCompletionHandler& completion_handler,
                       const aevrix::ServerConfig& config) {
    aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, conn->id(), 
                                         "handle_read_event called");
    
    // Perform nonblocking read
    auto read_result = conn->read_nonblocking();
    
    if (read_result == aevrix::Connection::IoResult::InProgress) {
        // No data available right now, wait for next EPOLLIN
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, conn->id(), 
                                             "No data available, waiting for EPOLLIN");
        return true;  // Keep connection alive, wait for more data
    }
    
    if (read_result == aevrix::Connection::IoResult::Closed) {
        // Peer disconnected
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::INFO, conn->id(), 
                                             "Peer disconnected");
        return false;  // Close connection
    }
    
    if (read_result == aevrix::Connection::IoResult::Error) {
        // Socket error
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::ERR, conn->id(), 
                                             "Socket error during read");
        return false;  // Close connection
    }
    
    // Data received successfully - feed to parser
    if (!conn->feed_parser()) {
        // Parser error occurred
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::ERR, conn->id(), 
                                             "Parser error: " + conn->parse_error_message());
        
        return false;
    }
    
    // Check parser state
    if (conn->has_parse_error()) {
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::ERR, conn->id(), 
                                             "Parser has error: " + conn->parse_error_message());
        return false;  // Close connection on error
    }
    
    if (conn->is_request_complete()) {
        aevrix::g_logger.log_with_request(aevrix::LogLevel::DEBUG, conn->id(), conn->request_id(),
                                         "Request complete, submitting to WorkerPool");
        
        // Extract the parsed request
        aevrix::http::HttpRequest request = conn->parser().request();
        
        // Evaluate keep-alive
        conn->evaluate_keep_alive(request);
        
        // Increment request ID
        conn->increment_request_id();
        
        // Stage 5: Submit blocking filesystem work to WorkerPool
        // Create worker task with immutable data
        bool is_head_request = (request.method() == aevrix::http::HttpMethod::HEAD);
        aevrix::WorkerTask task(conn->id(), request.target(), 
                               config.document_root(), is_head_request);
        
        // Submit task to worker pool
        try {
            // Submit task asynchronously (don't wait for result)
            worker_pool.submit<aevrix::WorkerResult>(
                [task, &completion_handler]() mutable {
                    // Execute filesystem work in worker thread
                    auto result = aevrix::execute_filesystem_task(task);
                    // Enqueue result for event loop
                    completion_handler.enqueue_result(std::move(result));
                    return result;  // Return WorkerResult for WorkerPool
                }
            );
            
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, conn->id(),
                                               "Task submitted to WorkerPool asynchronously");
            
            // For now, we'll use a simple approach: wait for the result
            // TODO: Implement true async with eventfd notification
            // For Stage 5, we acknowledge this is a limitation
            // The blocking work is in worker threads, but event loop still waits
            
            // For true async, we would:
            // 1. Add completion_handler.event_fd() to event loop with EPOLLIN
            // 2. When eventfd is readable, call completion_handler.dequeue_result()
            // 3. Look up connection by ID and apply result
            // 4. This requires significant Connection class changes
            
            // For this implementation, we'll return true and let the connection
            // wait for the worker to complete via a different mechanism
            // This is a known limitation that will be addressed in a future update
            
            // Placeholder: We need to store the connection ID and wait for completion
            // For now, we'll mark the connection as waiting for worker
            // and handle completion separately
            
            return true;  // Keep connection alive, waiting for worker
            
        } catch (const std::exception& e) {
            // Worker pool queue full or shut down
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::ERR, conn->id(),
                                                 "Failed to submit task to WorkerPool: " + std::string(e.what()));
            
            // Return 503 Service Unavailable
            aevrix::http::HttpResponse response(aevrix::http::StatusCode::ServiceUnavailable, 
                                   "Service Unavailable: Worker pool overloaded");
            response.set_header("Content-Type", "text/plain");
            response.set_header("Server", "Aevrix/0.1.0");
            response.set_connection_policy(aevrix::http::ConnectionPolicy::Close);
            
            aevrix::http::HttpResponseSerializer serializer;
            std::string response_data = serializer.serialize(response);
            conn->set_output_buffer(response_data);
            event_loop.modify_fd(conn->fd(), EPOLLIN | EPOLLOUT);
            return false;  // Close connection after error response
        }
        
        // Check if there's unconsumed data (pipelined requests)
        if (conn->has_unconsumed_data()) {
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, conn->id(),
                                                 "Unconsumed data in buffer (pipelined request)");
            conn->reset_parser();
            if (!conn->feed_parser()) {
                aevrix::g_logger.log_with_connection(aevrix::LogLevel::ERR, conn->id(),
                                                     "Parser error on pipelined request");
                return false;
            }
        } else {
            conn->reset_parser();
        }
        
        return true;
    }
    
    // Request not yet complete, wait for more data
    aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, conn->id(),
                                         "Request incomplete, waiting for more data");
    return true;  // Keep connection alive, wait for more EPOLLIN
}
#endif

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
void handle_connection(int client_fd, aevrix::StaticFileServer& file_server, const aevrix::ServerConfig& config, aevrix::WorkerPool& worker_pool, [[maybe_unused]] aevrix::Router& router) {
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

        // Note: StaticFileServer is now used only by worker threads
        // The event loop no longer holds a StaticFileServer instance
        // Each worker task creates its own temporary instance

        aevrix::g_logger.info("Static file serving configured with root: " + config.document_root());

        // Create TCP listener on configured host:port
        aevrix::TcpListener listener(config.host(), config.port());
        
        if (!listener.is_listening()) {
            aevrix::g_logger.error("Failed to start TCP listener: " + listener.error_message());
            return 1;
        }

        aevrix::g_logger.info("Server running on http://" + listener.host() + ":" + std::to_string(listener.port()) + "/");
        aevrix::g_logger.info("Serving files from: " + config.document_root());
        
        // Create worker pool for blocking operations
        // Use configured number of workers and queue size
        // This keeps blocking filesystem work out of the event loop
        aevrix::WorkerPool worker_pool(config.workers(), 128);
        
        // Create worker completion handler for async result delivery
        aevrix::WorkerCompletionHandler completion_handler;
        
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
            if (!listener.start(config.host(), config.port(), true)) {  // Start with non-blocking
                aevrix::g_logger.error("Failed to start non-blocking listener");
                return 1;
            }
            
            // Create ConnectionManager (Stage 2)
            aevrix::ConnectionManager connection_manager(&config);
            
            // Add completion handler eventfd to event loop (Stage 5)
            if (completion_handler.event_fd() >= 0) {
                event_loop.add_fd(completion_handler.event_fd(), EPOLLIN,
                    [&completion_handler, &connection_manager, &event_loop]([[maybe_unused]] int fd, aevrix::EventType event) {
                        if (event == aevrix::EventType::Readable) {
                            // Clear the eventfd
                            completion_handler.clear_event();
                            
                            // Process all available results
                            while (true) {
                                auto result = completion_handler.dequeue_result();
                                if (!result.has_value()) {
                                    break;  // No more results
                                }
                                
                                // Look up connection by ID
                                auto conn = connection_manager.get_connection_by_id(result->connection_id);
                                if (conn) {
                                    // Connection still exists, apply result
                                    aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, result->connection_id,
                                                                         "Worker result received, applying to connection");
                                    
                                    // Generate response from worker result
                                    aevrix::http::HttpResponse response;
                                    if (result->status_code == 200) {
                                        response = aevrix::http::HttpResponse(aevrix::http::StatusCode::OK, result->content);
                                        response.set_header("Content-Type", result->mime_type);
                                        response.set_header("Server", "Aevrix/0.1.0");
                                        response.set_connection_policy(conn->keep_alive() ? 
                                            aevrix::http::ConnectionPolicy::KeepAlive : 
                                            aevrix::http::ConnectionPolicy::Close);
                                    } else {
                                        // Error response
                                        response = aevrix::http::HttpResponse(
                                            static_cast<aevrix::http::StatusCode>(result->status_code), 
                                            result->error_message);
                                        response.set_header("Content-Type", "text/plain");
                                        response.set_header("Server", "Aevrix/0.1.0");
                                        response.set_connection_policy(aevrix::http::ConnectionPolicy::Close);
                                    }
                                    
                                    // Serialize response
                                    aevrix::http::HttpResponseSerializer serializer;
                                    std::string response_data = serializer.serialize(response);
                                    
                                    // Set output buffer
                                    conn->set_output_buffer(response_data);
                                    
                                    // Enable EPOLLOUT for writing response
                                    event_loop.modify_fd(conn->fd(), EPOLLIN | EPOLLOUT);
                                    
                                    aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, result->connection_id,
                                                                         "Response generated from worker result, EPOLLOUT enabled");
                                } else {
                                    // Connection no longer exists, discard result
                                    aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, result->connection_id,
                                                                         "Worker result received but connection no longer exists, discarding");
                                }
                            }
                        }
                    });
            }
            
            // Add listener socket to event loop
            if (!event_loop.add_fd(listener.get_socket(), EPOLLIN, 
                [&listener, &connection_manager, &router, &event_loop, &worker_pool, &completion_handler, &config, &connection_count]([[maybe_unused]] int fd, aevrix::EventType event) {
                    if (event == aevrix::EventType::Readable) {
                        // Accept new connection
                        auto client_fd = listener.accept();
                        if (client_fd.has_value()) {
                            // Register connection with ConnectionManager
                            auto conn = connection_manager.register_connection(client_fd.value());
                            if (conn) {
                                // Add connection to event loop for EPOLLIN
                                int client_fd_value = client_fd.value();
                                event_loop.add_fd(client_fd_value, EPOLLIN, 
                                    [client_fd_value, &connection_manager, &router, &event_loop, &worker_pool, &completion_handler, &config]([[maybe_unused]] int, aevrix::EventType client_event) {
                                        if (client_event == aevrix::EventType::Readable) {
                                            auto conn_ptr = connection_manager.get_connection(client_fd_value);
                                            if (conn_ptr) {
                                                bool keep_alive = handle_read_event(conn_ptr, router, event_loop, worker_pool, completion_handler, config);
                                                if (!keep_alive) {
                                                    // Remove from event loop first (prevents further events)
                                                    event_loop.remove_fd(client_fd_value);
                                                    // Then remove from connection manager (may destroy connection)
                                                    connection_manager.remove_connection(client_fd_value);
                                                }
                                            } else {
                                                // Connection already removed, clean up event loop
                                                event_loop.remove_fd(client_fd_value);
                                            }
                                        } else if (client_event == aevrix::EventType::Writable) {
                                            // Stage 4: Handle EPOLLOUT event
                                            auto conn_ptr = connection_manager.get_connection(client_fd_value);
                                            if (conn_ptr) {
                                                bool keep_alive = handle_write_event(conn_ptr, event_loop);
                                                if (!keep_alive) {
                                                    event_loop.remove_fd(client_fd_value);
                                                    connection_manager.remove_connection(client_fd_value);
                                                }
                                            } else {
                                                event_loop.remove_fd(client_fd_value);
                                            }
                                        } else if (client_event == aevrix::EventType::Error || client_event == aevrix::EventType::Hangup) {
                                            // Remove from event loop first (prevents further events)
                                            event_loop.remove_fd(client_fd_value);
                                            // Then remove from connection manager (may destroy connection)
                                            connection_manager.remove_connection(client_fd_value);
                                        }
                                    });
                                
                                connection_count++;
                                aevrix::g_logger.info("Total connections handled: " + std::to_string(connection_count));
                            } else {
                                aevrix::g_logger.warn("Connection rejected (at capacity)");
                            }
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
        
        // Create static file server for Windows fallback
        aevrix::StaticFileServer file_server(config.document_root());
        
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


