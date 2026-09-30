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
#include "aevrix/server_config_store.h"
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
#include "aevrix/proxy_handler.h"
#include "aevrix/proxy_request_builder.h"
#include "aevrix/websocket_handshake.h"
#endif
#ifdef AEVRIX_ENABLE_TLS
#include "aevrix/tls_context.h"
#endif
#include <iostream>
#include <string>
#include <cstdint>  // For uint16_t
#include <vector>   // For command-line arguments
#include <memory>   // For std::unique_ptr
#include <chrono>   // For timing
#include <functional>  // For std::function (Phase 24 reload entry point)
#include <sstream>  // For admin status bodies

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
#ifdef __linux__
#include <sys/eventfd.h>  // Phase 24: SIGHUP wake-up descriptor
#endif
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
    error_response.set_header("Server", "Aevrix/1.0.0");
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
// =============================================================================
// Phase 24: configuration reload helpers
// =============================================================================

namespace {

/// Join configuration key names for a plain-text admin response.
std::string join_keys(const std::vector<std::string>& keys) {
    std::string out;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += keys[i];
    }
    return out;
}

/// Decode %XX escapes so a config path survives URL encoding in ?path=.
std::string percent_decode(const std::string& value) {
    const auto hex_digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = hex_digit(value[i + 1]);
            const int lo = hex_digit(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += value[i];
    }
    return out;
}

/// Config path requested by an admin reload: ?path=..., "path=<file>" or a bare path.
std::string requested_config_path(const HttpRequest& request) {
    const std::string& target = request.target();
    const size_t query_start = target.find('?');
    if (query_start != std::string::npos) {
        const std::string query = target.substr(query_start + 1);
        size_t pos = 0;
        while (pos <= query.size()) {
            const size_t amp = query.find('&', pos);
            const std::string pair =
                query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            const size_t eq = pair.find('=');
            if (eq != std::string::npos && pair.substr(0, eq) == "path") {
                return percent_decode(pair.substr(eq + 1));
            }
            if (amp == std::string::npos) {
                break;
            }
            pos = amp + 1;
        }
    }

    const std::string& body = request.body();
    const size_t first = body.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";  // No override: reload the configured file
    }
    const size_t last = body.find_last_not_of(" \t\r\n");
    std::string trimmed = body.substr(first, last - first + 1);
    const std::string prefix = "path=";
    if (trimmed.rfind(prefix, 0) == 0) {
        return trimmed.substr(prefix.size());
    }
    return trimmed;
}

/// Bearer-token check for the /admin endpoints (empty token = auth disabled).
bool admin_token_ok(const HttpRequest& request, const aevrix::ServerConfig& config) {
    const std::string& expected = config.admin_token();
    if (expected.empty()) {
        return true;  // No token configured: rely on bind address / network policy
    }

    std::string presented = request.get_header("Authorization");
    const std::string bearer = "Bearer ";
    if (presented.rfind(bearer, 0) == 0) {
        presented = presented.substr(bearer.size());
    } else {
        presented = request.get_header("X-Aevrix-Token");
        if (presented.empty()) {
            presented = request.get_header("Authorization");
        }
    }

    // Compare without early exit so a wrong token does not leak its prefix.
    if (presented.size() != expected.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (size_t i = 0; i < expected.size(); ++i) {
        difference |= static_cast<unsigned char>(presented[i] ^ expected[i]);
    }
    return difference == 0;
}

/// An override path must be absolute, free of "..", and inside the configured
/// file's directory: reloading arbitrary files is not something a remote caller
/// gets to do.
bool reload_path_allowed(const std::string& candidate, const std::string& source) {
    if (candidate.empty()) {
        return true;  // No override
    }
    if (candidate[0] != '/' || candidate.find("..") != std::string::npos) {
        return false;
    }
    if (source.empty() || source[0] != '/') {
        return false;  // No configured file: no directory to stay inside
    }
    const size_t slash = source.find_last_of('/');
    if (slash == std::string::npos) {
        return false;
    }
    const std::string directory = source.substr(0, slash + 1);  // keeps the '/'
    return candidate.rfind(directory, 0) == 0 && candidate.size() > directory.size();
}

} // namespace

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
                response.set_header("Server", "Aevrix/1.0.0");
                response.set_connection_policy(keep_alive ? ConnectionPolicy::KeepAlive : ConnectionPolicy::Close);
                return response;
            } else {
                // HEAD request - return 200 OK with no body
                HttpResponse response(StatusCode::OK);
                response.set_header("Content-Type", mime_type);
                response.set_header("Content-Length", std::to_string(content.length()));
                response.set_header("Server", "Aevrix/1.0.0");
                response.set_connection_policy(keep_alive ? ConnectionPolicy::KeepAlive : ConnectionPolicy::Close);
                return response;
            }
        } else if (status_code == 404) {
            // File not found
            HttpResponse response(StatusCode::NotFound, "Not Found");
            response.set_header("Content-Type", "text/plain");
            response.set_header("Server", "Aevrix/1.0.0");
            response.set_connection_policy(ConnectionPolicy::Close);  // Always close on error
            return response;
        } else if (status_code == 403) {
            // Forbidden (directory access or path traversal attempt)
            HttpResponse response(StatusCode::Forbidden, "Forbidden");
            response.set_header("Content-Type", "text/plain");
            response.set_header("Server", "Aevrix/1.0.0");
            response.set_connection_policy(ConnectionPolicy::Close);  // Always close on error
            return response;
        } else {
            // Internal server error
            HttpResponse response(StatusCode::InternalServerError, "Internal Server Error");
            response.set_header("Content-Type", "text/plain");
            response.set_header("Server", "Aevrix/1.0.0");
            response.set_connection_policy(ConnectionPolicy::Close);  // Always close on error
            return response;
        }
    } else {
        // Return 405 Method Not Allowed for unsupported methods
        HttpResponse response(StatusCode::MethodNotAllowed, 
                               "Method not allowed: " + aevrix::http::http_method_to_string(request.method()));
        response.set_header("Content-Type", "text/plain");
        response.set_header("Server", "Aevrix/1.0.0");
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
        response.set_header("Server", "Aevrix/1.0.0");
        response.set_connection_policy(aevrix::http::ConnectionPolicy::Close);
        return response;
    }
}

/**
 * @brief Convert a proxy outcome into the response sent to the client (Phase 22)
 *
 * The upstream response is re-framed: hop-by-hop headers and the upstream's own
 * framing headers are dropped, and Content-Length is regenerated from the body
 * by HttpResponse itself. That guarantees the client always sees a
 * self-consistent message even if the upstream used chunked encoding or closed
 * the connection to delimit its body.
 *
 * @param outcome The upstream result
 * @param client_keep_alive Whether the client asked for keep-alive
 * @return HttpResponse The response to send to the client
 */
HttpResponse build_proxy_response(const aevrix::ProxyOutcome& outcome, bool client_keep_alive) {
    if (outcome.status == aevrix::ProxyOutcomeStatus::Success) {
        const uint16_t upstream_code = static_cast<uint16_t>(outcome.upstream_status);
        const HttpResponse response(static_cast<StatusCode>(upstream_code), outcome.body);
        HttpResponse result = response;

        // Relay end-to-end headers only.
        for (const auto& header : outcome.headers) {
            const std::string& normalized = header.normalized_name();

            if (aevrix::is_hop_by_hop_header(normalized) || normalized == "content-length") {
                continue;
            }

            result.set_header(header.name(), header.value());
        }

        // A HEAD response carries the framing of the equivalent GET, so the
        // upstream Content-Length is preserved instead of being regenerated,
        // and no body bytes may follow the header block (RFC 9110 Section 9.3.2).
        if (outcome.head_request) {
            result.set_head_only(true);
            const std::string upstream_length = outcome.headers.get("Content-Length");
            if (!upstream_length.empty()) {
                result.set_header("Content-Length", upstream_length);
            } else {
                // The representation length is unknown upstream (for example a
                // chunked GET); the constructor's "0" would misreport the
                // resource size, so the header is omitted entirely.
                result.headers().remove("Content-Length");
            }
        }

        result.set_header("Server", "Aevrix/1.0.0");
        result.set_header("Via", "1.1 aevrix");
        result.set_connection_policy(client_keep_alive ? ConnectionPolicy::KeepAlive
                                                      : ConnectionPolicy::Close);
        return result;
    }

    // -------------------------------------------------------------------------
    // Upstream failure: map the outcome onto a gateway status code.
    // -------------------------------------------------------------------------
    StatusCode status = StatusCode::BadGateway;
    std::string message = "Bad Gateway: the upstream server could not be reached";

    switch (outcome.status) {
        case aevrix::ProxyOutcomeStatus::GatewayTimeout:
            status = StatusCode::GatewayTimeout;
            message = "Gateway Timeout: the upstream server did not respond in time";
            break;
        case aevrix::ProxyOutcomeStatus::Unavailable:
            status = StatusCode::ServiceUnavailable;
            message = "Service Unavailable: no upstream capacity available";
            break;
        case aevrix::ProxyOutcomeStatus::UpstreamTooLarge:
            message = "Bad Gateway: the upstream response exceeded the configured limit";
            break;
        case aevrix::ProxyOutcomeStatus::NotConfigured:
            message = "Bad Gateway: the reverse proxy is not configured correctly";
            break;
        case aevrix::ProxyOutcomeStatus::BadGateway:
        case aevrix::ProxyOutcomeStatus::Success:
        default:
            if (!outcome.error_message.empty()) {
                message = "Bad Gateway: " + outcome.error_message;
            }
            break;
    }

    HttpResponse response(status, message + "\n");
    response.set_header("Content-Type", "text/plain");
    response.set_header("Server", "Aevrix/1.0.0");
    response.set_header("Via", "1.1 aevrix");
    // An error reply to HEAD keeps the Content-Length metadata but must not
    // push body bytes onto a connection that will never read them.
    if (outcome.head_request) {
        response.set_head_only(true);
    }
    // Errors are terminal for this exchange: closing avoids ambiguity about
    // whether an unread request body is still on the wire.
    response.set_connection_policy(ConnectionPolicy::Close);
    return response;
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
                        aevrix::EventLoop& event_loop,
                        const aevrix::ServerConfig& config) {
    aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, conn->id(),
                                         "handle_write_event called");

    // Stage 6: Update deadline on write activity
    conn->set_deadline(config);

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

        // Phase 23: an upgraded connection flushes frames instead of cycling
        // through HTTP keep-alive. Once both Close frames have been exchanged
        // and everything is flushed, the TCP connection may be dropped.
        if (conn->is_websocket()) {
            auto* ws_session = conn->websocket();

            if (ws_session->has_pending_output()) {
                // Frames queued while the previous batch was flushing.
                conn->append_output_buffer(ws_session->take_output());
                conn->set_write_state(aevrix::WriteState::Body);
                event_loop.modify_fd(conn->fd(), EPOLLIN | EPOLLOUT);
                conn->set_deadline(config);
                return true;
            }

            conn->set_write_state(aevrix::WriteState::Complete);
            conn->set_deadline(config);

            if (ws_session->ready_to_close()) {
                aevrix::g_logger.log_with_connection(aevrix::LogLevel::INFO, conn->id(),
                                                     "WebSocket close handshake complete");
                return false;  // Close TCP connection
            }

            // Back to frame reads only (EPOLLOUT disarmed).
            event_loop.modify_fd(conn->fd(), EPOLLIN);
            return true;
        }

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
                       aevrix::Router& router,
                       aevrix::EventLoop& event_loop,
                       aevrix::WorkerPool& worker_pool,
                       aevrix::WorkerCompletionHandler& completion_handler,
                       aevrix::ProxyHandler& proxy_handler,
                       const aevrix::ServerConfig& config) {
    aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, conn->id(),
                                         "handle_read_event called");

    // Stage 6: Update deadline on read activity
    conn->set_deadline(config);

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

    // Phase 23: once upgraded, raw bytes belong to the WebSocket frame layer
    // and the HTTP parser is bypassed entirely (separation of concerns).
    if (conn->is_websocket()) {
        auto* ws_session = conn->websocket();
        ws_session->feed(conn->input_buffer().data(), conn->input_buffer().size());
        conn->clear_input_buffer();
        conn->update_activity();

        if (ws_session->had_protocol_error()) {
            aevrix::g_logger.log_with_connection(aevrix::LogLevel::WARN, conn->id(),
                                                 "WebSocket protocol error, close frame queued");
        }

        if (ws_session->has_pending_output()) {
            conn->append_output_buffer(ws_session->take_output());
            conn->set_write_state(aevrix::WriteState::Body);
            event_loop.modify_fd(conn->fd(), EPOLLIN | EPOLLOUT);
        }
        conn->set_deadline(config);
        return true;  // Close decision happens when the output flushes
    }

    // Data received successfully - feed to parser
    if (!conn->feed_parser()) {
        // Parser error occurred
        aevrix::g_logger.log_with_connection(aevrix::LogLevel::ERR, conn->id(),
                                             "Parser error: " + conn->parse_error_message());

        return false;
    }

#ifdef AEVRIX_ENABLE_TLS
    // Phase 21: Handle TLS WANT_READ/WANT_WRITE after read
    if (conn->is_tls_enabled() && conn->tls_connection()) {
        // Check if TLS needs different epoll interest
        // This is handled by the TLS read path in Connection::read_nonblocking()
        // If SSL_read returned WANT_WRITE, we need to enable EPOLLOUT
        // For now, we'll check the connection's TLS state and update accordingly
        // A more sophisticated approach would track the TLS I/O requirement explicitly
    }
#endif

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

        // Phase 24: application routes (exact method + path) are served first,
        // so operators can register endpoints such as /admin/reload-config.
        // The router only maps Request -> Response; the socket stays here.
        const std::string route_method = aevrix::http::http_method_to_string(request.method());
        if (router.has_route(route_method, request.target())) {
            const aevrix::http::HttpResponse routed = router.route(request);
            const std::string serialized =
                aevrix::http::HttpResponseSerializer::serialize(routed);

            if (serialized.empty()) {
                // Nothing to send: a failed exchange must fail fast, not hang.
                aevrix::g_logger.log_with_request(aevrix::LogLevel::ERR, conn->id(),
                                                  conn->request_id(),
                                                  "Router produced an empty response, closing");
                return false;
            }

            conn->set_output_buffer(serialized);
            conn->set_state(aevrix::ConnectionState::Writing);
            conn->set_write_state(aevrix::WriteState::Body);
            conn->set_deadline(config);
            event_loop.modify_fd(conn->fd(), EPOLLIN | EPOLLOUT);

            aevrix::g_logger.log_with_request(aevrix::LogLevel::INFO, conn->id(),
                                              conn->request_id(),
                                              "Routed " + route_method + " " + request.target());
            return true;
        }

        // Phase 23: WebSocket upgrade (RFC 6455). Evaluated before the proxy
        // and worker paths because Aevrix terminates WebSocket itself: a
        // valid upgrade swaps the connection into frame mode with a 101
        // response, while a malformed upgrade attempt is rejected outright.
        if (config.websocket_enabled()) {
            const aevrix::ws::UpgradeResult upgrade =
                aevrix::ws::evaluate_upgrade(request, config);

            if (upgrade.verdict == aevrix::ws::UpgradeVerdict::Accepted) {
                conn->begin_websocket(config.websocket_max_message_bytes());
                conn->set_output_buffer(upgrade.response);
                conn->set_keep_alive(false);
                conn->set_state(aevrix::ConnectionState::Writing);
                conn->set_write_state(aevrix::WriteState::Body);
                conn->set_deadline(config);
                event_loop.modify_fd(conn->fd(), EPOLLIN | EPOLLOUT);

                aevrix::g_logger.log_with_request(aevrix::LogLevel::INFO, conn->id(),
                                                  conn->request_id(),
                                                  "WebSocket upgrade accepted: " + request.target());
                return true;
            }

            if (upgrade.verdict != aevrix::ws::UpgradeVerdict::NotAnUpgrade) {
                const aevrix::http::HttpResponse rejection =
                    aevrix::ws::build_rejection_response(upgrade.verdict);
                conn->set_output_buffer(
                    aevrix::http::HttpResponseSerializer::serialize(rejection));
                conn->set_keep_alive(false);
                conn->set_state(aevrix::ConnectionState::Writing);
                conn->set_write_state(aevrix::WriteState::Body);
                conn->set_deadline(config);
                event_loop.modify_fd(conn->fd(), EPOLLIN | EPOLLOUT);

                aevrix::g_logger.log_with_request(aevrix::LogLevel::WARN, conn->id(),
                                                  conn->request_id(),
                                                  "WebSocket upgrade rejected ("
                                                      + std::to_string(static_cast<int>(upgrade.verdict))
                                                      + "): " + request.target());
                return true;
            }
        }

        // Phase 22: requests below the proxy prefix are forwarded to the
        // upstream. The exchange is asynchronous: ProxyHandler drives it from
        // the event loop and reports back through its completion callback.
        if (proxy_handler.handles(request)) {
            // While the upstream is working the client is idle, so its own
            // deadlines are suspended exactly as they are for a worker task.
            conn->set_worker_active(true);
            proxy_handler.start(conn->id(), conn->fd(), request);
            return true;
        }

        // Stage 5: Submit blocking filesystem work to WorkerPool
        // Create worker task with immutable data
        bool is_head_request = (request.method() == aevrix::http::HttpMethod::HEAD);
        aevrix::WorkerTask task(conn->id(), request.target(),
                               config.document_root(), is_head_request);

        // Submit task to worker pool
        try {
            // Stage 6: Mark worker as active (don't timeout while worker is running)
            conn->set_worker_active(true);

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
            response.set_header("Server", "Aevrix/1.0.0");
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

        // Phase 24: apply the configured log level before the startup banner so
        // the verbosity is already correct, then publish the validated config
        // through an atomic store.
        //
        // The store is the single source of truth for everything that can
        // change at runtime: event callbacks and sweeps read a snapshot instead
        // of the `config` object below. `config` stays the startup-only view of
        // values that need a restart (listener address/port, TLS contexts,
        // worker-thread count).
        if (!aevrix::g_logger.set_level_from_string(config.log_level())) {
            aevrix::g_logger.warn("Unknown log_level '" + config.log_level() +
                                  "', keeping the current verbosity");
        }
        aevrix::ServerConfigStore config_store(
            std::make_shared<aevrix::ServerConfig>(config), config_file);

        // Phase 24: single reload entry point. Defined once proxy_handler
        // exists (below); the SIGHUP wake-up callback, the /admin route and the
        // maintenance pass all go through it, always on the event-loop thread.
        std::function<aevrix::ConfigReloadOutcome(const std::string&)> perform_reload;

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

#ifdef AEVRIX_ENABLE_TLS
        // Phase 21: Initialize TLS context if TLS is enabled
        std::unique_ptr<aevrix::TlsContext> tls_context;
        std::unique_ptr<aevrix::TcpListener> tls_listener;

        if (config.tls_enabled()) {
            aevrix::g_logger.info("TLS enabled, initializing TLS context");

            try {
                // Create TLS context
                tls_context = std::make_unique<aevrix::TlsContext>();

                // Load certificate and private key
                tls_context->load_certificate_and_key(config.tls_cert_file(), config.tls_key_file());

                // Configure protocol versions if specified
                if (!config.tls_min_version().empty()) {
                    tls_context->set_min_protocol_version(config.tls_min_version());
                }
                if (!config.tls_max_version().empty()) {
                    tls_context->set_max_protocol_version(config.tls_max_version());
                }

                aevrix::g_logger.info("TLS context initialized successfully");

                // Create TLS listener on configured TLS port
                tls_listener = std::make_unique<aevrix::TcpListener>(config.host(), config.tls_port());

                if (!tls_listener->is_listening()) {
                    aevrix::g_logger.error("Failed to start TLS listener: " + tls_listener->error_message());
                    return 1;
                }

                aevrix::g_logger.info("TLS listener started on " + config.host() + ":" + std::to_string(config.tls_port()));
            } catch (const std::exception& e) {
                aevrix::g_logger.error("TLS initialization failed: " + std::string(e.what()));
                return 1;
            }
        } else {
            aevrix::g_logger.info("TLS disabled, running in plaintext mode");
        }
#endif
        
        // Register GET / route (root endpoint)
        router.add_route("GET", "/", [](const HttpRequest& request) {
            (void)request;  // Root endpoint doesn't need request details
            HttpResponse response(StatusCode::OK, "Aevrix HTTP Server v1.0.0\n");
            response.set_header("Content-Type", "text/plain");
            response.set_header("Server", "Aevrix/1.0.0");
            response.set_connection_policy(ConnectionPolicy::KeepAlive);
            return response;
        });
        
        // Register GET /health route (health check endpoint)
        router.add_route("GET", "/health", [](const HttpRequest& request) {
            (void)request;  // Health check doesn't need request details
            HttpResponse response(StatusCode::OK, "OK\n");
            response.set_header("Content-Type", "text/plain");
            response.set_header("Server", "Aevrix/1.0.0");
            response.set_connection_policy(ConnectionPolicy::KeepAlive);
            return response;
        });
        
        // Register GET /metrics route (metrics endpoint)
        router.add_route("GET", "/metrics", [](const HttpRequest& request) {
            (void)request;  // Metrics endpoint doesn't need request details yet
            HttpResponse response(StatusCode::OK, "Metrics endpoint - not yet implemented\n");
            response.set_header("Content-Type", "text/plain");
            response.set_header("Server", "Aevrix/1.0.0");
            response.set_connection_policy(ConnectionPolicy::KeepAlive);
            return response;
        });
        
        // ---------------------------------------------------------------------
        // Phase 24: configuration administration endpoints
        // ---------------------------------------------------------------------
        // Opt-in via admin_api_enabled; when it is false both routes answer 404
        // so the surface stays invisible to scanners. When admin_token is set,
        // callers must present "Authorization: Bearer <token>" (or
        // "X-Aevrix-Token"). Both handlers run on the event-loop thread, which
        // is also where SIGHUP reloads are applied, so they never race with a
        // signal-driven reload.
        router.add_route("GET", "/admin/config", [&config_store](const HttpRequest& request) {
            const std::shared_ptr<const aevrix::ServerConfig> live = config_store.snapshot();
            HttpResponse response;

            if (!live->admin_api_enabled()) {
                response = HttpResponse(StatusCode::NotFound, "Not Found\n");
                response.set_header("Content-Type", "text/plain");
            } else if (!admin_token_ok(request, *live)) {
                response = HttpResponse(StatusCode::Unauthorized, "Unauthorized\n");
                response.set_header("Content-Type", "text/plain");
                response.set_header("WWW-Authenticate", "Bearer realm=\"aevrix-admin\"");
            } else {
                std::ostringstream body;
                body << "generation=" << config_store.generation() << "\n";
                body << "config_path=" << config_store.source_path() << "\n";
                body << "reload_successes=" << config_store.reload_success_count() << "\n";
                body << "reload_failures=" << config_store.reload_failure_count() << "\n";
                const std::string last_error = config_store.last_error();
                if (!last_error.empty()) {
                    body << "last_error=" << last_error << "\n";
                }
                body << live->summary();

                response = HttpResponse(StatusCode::OK, body.str());
                response.set_header("Content-Type", "text/plain");
                response.set_header("X-Aevrix-Config-Generation",
                                    std::to_string(config_store.generation()));
            }

            response.set_header("Server", "Aevrix/1.0.0");
            response.set_connection_policy(ConnectionPolicy::KeepAlive);
            return response;
        });

        router.add_route("POST", "/admin/reload-config",
                         [&config_store, &perform_reload](const HttpRequest& request) {
            const std::shared_ptr<const aevrix::ServerConfig> live = config_store.snapshot();
            HttpResponse response;

            if (!live->admin_api_enabled()) {
                response = HttpResponse(StatusCode::NotFound, "Not Found\n");
                response.set_header("Content-Type", "text/plain");
            } else if (!admin_token_ok(request, *live)) {
                response = HttpResponse(StatusCode::Unauthorized, "Unauthorized\n");
                response.set_header("Content-Type", "text/plain");
                response.set_header("WWW-Authenticate", "Bearer realm=\"aevrix-admin\"");
            } else if (!perform_reload) {
                response = HttpResponse(StatusCode::NotImplemented,
                                        "Reload endpoint unavailable on this platform\n");
                response.set_header("Content-Type", "text/plain");
            } else {
                const std::string override_path = requested_config_path(request);
                if (!reload_path_allowed(override_path, config_store.source_path())) {
                    response = HttpResponse(
                        StatusCode::BadRequest,
                        "error=config path must be an absolute file inside the configured "
                        "configuration directory\n");
                    response.set_header("Content-Type", "text/plain");
                } else {
                    const aevrix::ConfigReloadOutcome outcome = perform_reload(override_path);

                    std::ostringstream body;
                    body << (outcome.success ? "reload: applied" : "reload: rejected") << "\n";
                    body << "generation=" << outcome.generation << "\n";
                    if (outcome.success) {
                        body << "config_path=" << config_store.source_path() << "\n";
                        body << "changed=" << join_keys(outcome.changes) << "\n";
                        body << "applied_now=" << join_keys(outcome.applied_hot) << "\n";
                        body << "restart_required=" << join_keys(outcome.restart_required) << "\n";
                    } else {
                        body << "error=" << outcome.message << "\n";
                    }

                    response = HttpResponse(
                        outcome.success ? StatusCode::OK : StatusCode::BadRequest, body.str());
                    response.set_header("Content-Type", "text/plain");
                    response.set_header("X-Aevrix-Config-Generation",
                                        std::to_string(outcome.generation));
                }
            }

            response.set_header("Server", "Aevrix/1.0.0");
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
            
            // Create ConnectionManager (Stage 2). Phase 24: it also receives the
            // live config store so max_connections follows reloads.
            aevrix::ConnectionManager connection_manager(&config, &config_store);
            
            // Phase 22: reverse proxy handler.
            // It owns the upstream half of proxied requests and reports each
            // result through this callback, which reuses the same
            // "output buffer + EPOLLOUT" path as static-file responses.
            aevrix::ProxyHandler proxy_handler(
                config, event_loop,
                [&connection_manager, &event_loop, &config_store](uint64_t client_connection_id,
                                                            aevrix::ProxyOutcome outcome) {
                    auto conn = connection_manager.get_connection_by_id(client_connection_id);
                    if (!conn) {
                        aevrix::g_logger.log_with_connection(
                            aevrix::LogLevel::DEBUG, client_connection_id,
                            "Proxy result discarded: client connection no longer exists");
                        return;
                    }

                    // The exchange is over, so normal timeout accounting resumes.
                    // Phase 24: the deadline uses a fresh snapshot, so a reload
                    // that changed the keep-alive/write budgets applies here.
                    conn->set_worker_active(false);
                    conn->set_deadline(*config_store.snapshot());

                    const aevrix::http::HttpResponse response =
                        build_proxy_response(outcome, conn->keep_alive());

                    aevrix::http::HttpResponseSerializer serializer;
                    const std::string serialized = serializer.serialize(response);

                    if (serialized.empty()) {
                        aevrix::g_logger.log_with_connection(
                            aevrix::LogLevel::ERR, client_connection_id,
                            "Proxy response failed to serialize; closing client connection");
                        // An empty output buffer reads as "already complete",
                        // so arming EPOLLOUT would leave the client waiting
                        // forever. Close instead: a failed exchange must fail
                        // fast, not hang.
                        event_loop.remove_fd(conn->fd());
                        connection_manager.remove_connection(conn->fd());
                        return;
                    }

                    conn->set_output_buffer(serialized);

                    event_loop.modify_fd(conn->fd(), EPOLLIN | EPOLLOUT);

                    aevrix::g_logger.log_with_connection(
                        aevrix::LogLevel::DEBUG, client_connection_id,
                        "Proxy response ready, EPOLLOUT enabled");
                },
                &config_store);

            // Phase 24: the single reload entry point. Called from the SIGHUP
            // wake-up callback, the /admin/reload-config route, or the 1s
            // maintenance pass - always on the event-loop thread, so no extra
            // locking is needed here.
            perform_reload = [&config_store, &proxy_handler](const std::string& path)
                                 -> aevrix::ConfigReloadOutcome {
                const aevrix::ConfigReloadOutcome outcome = config_store.try_reload(path);
                if (!outcome.success || !outcome.snapshot) {
                    return outcome;  // Rejected: the active generation is untouched
                }

                // Log verbosity is the one setting that must be pushed, because
                // the logger does not read the store itself. Everything else is
                // pulled from the snapshot by the code that needs it.
                aevrix::g_logger.set_level_from_string(outcome.snapshot->log_level());

                // Upstream pool bounds are held by a long-lived object, so they
                // are pushed too (routing and TLS still need a restart).
                proxy_handler.refresh_runtime_limits();
                return outcome;
            };
            
            // Add completion handler eventfd to event loop (Stage 5)
            if (completion_handler.event_fd() >= 0) {
                event_loop.add_fd(completion_handler.event_fd(), EPOLLIN,
                    [&completion_handler, &connection_manager, &event_loop, &config_store]([[maybe_unused]] int fd, aevrix::EventType event) {
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
                                    // Stage 6: Mark worker as inactive and update deadline
                                    // Phase 24: from a fresh snapshot, so a reload's new
                                    // keep-alive/write budget applies to this response.
                                    conn->set_worker_active(false);
                                    conn->set_deadline(*config_store.snapshot());

                                    // Connection still exists, apply result
                                    aevrix::g_logger.log_with_connection(aevrix::LogLevel::DEBUG, result->connection_id,
                                                                         "Worker result received, applying to connection");

                                    // Generate response from worker result
                                    aevrix::http::HttpResponse response;
                                    if (result->status_code == 200) {
                                        response = aevrix::http::HttpResponse(aevrix::http::StatusCode::OK, result->content);
                                        response.set_header("Content-Type", result->mime_type);
                                        response.set_header("Server", "Aevrix/1.0.0");
                                        response.set_connection_policy(conn->keep_alive() ? 
                                            aevrix::http::ConnectionPolicy::KeepAlive : 
                                            aevrix::http::ConnectionPolicy::Close);
                                    } else {
                                        // Error response
                                        response = aevrix::http::HttpResponse(
                                            static_cast<aevrix::http::StatusCode>(result->status_code), 
                                            result->error_message);
                                        response.set_header("Content-Type", "text/plain");
                                        response.set_header("Server", "Aevrix/1.0.0");
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
            
            // Phase 24: SIGHUP wake-up path. The signal handler only writes to
            // this eventfd (async-signal-safe); parsing, validation and the
            // config swap happen here, on the event-loop thread. Without it a
            // reload would still be picked up by the 1s maintenance pass, just
            // less promptly.
            aevrix::UniqueFd reload_event_fd(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
            if (reload_event_fd.is_valid()) {
                const int reload_fd = reload_event_fd.get();
                aevrix::g_signal_handler.set_reload_notify_fd(reload_fd);

                if (!event_loop.add_fd(reload_fd, EPOLLIN,
                    [&perform_reload]([[maybe_unused]] int fd, aevrix::EventType event) {
                        if (event != aevrix::EventType::Readable) {
                            return;
                        }
                        // Drain the counter (one 8-byte read per wake-up write).
                        uint64_t drains = 0;
                        while (::read(fd, &drains, sizeof(drains)) ==
                               static_cast<ssize_t>(sizeof(drains))) {
                        }

                        // A burst of SIGHUPs collapses into a single reload.
                        if (aevrix::g_signal_handler.consume_reload_request() && perform_reload) {
                            perform_reload("");
                        }
                    })) {
                    aevrix::g_logger.warn("Failed to register the reload eventfd with the event "
                                          "loop; falling back to the 1s maintenance poll");
                    aevrix::g_signal_handler.set_reload_notify_fd(-1);
                } else {
                    aevrix::g_logger.info("SIGHUP configuration reload enabled (eventfd-driven)");
                }
            } else {
                aevrix::g_logger.warn("eventfd unavailable; configuration reload will be polled "
                                      "once per second");
            }

            // Add listener socket to event loop
            if (!event_loop.add_fd(listener.get_socket(), EPOLLIN,
                [&listener, &connection_manager, &router, &event_loop, &worker_pool, &completion_handler, &proxy_handler, &config_store, &connection_count
#ifdef AEVRIX_ENABLE_TLS
                 , &tls_context
#endif
                ]([[maybe_unused]] int fd, aevrix::EventType event) {
                    if (event == aevrix::EventType::Readable) {
                        // Accept new connection
                        auto client_fd = listener.accept();
                        if (client_fd.has_value()) {
                            // Register connection with ConnectionManager
                            auto conn = connection_manager.register_connection(client_fd.value());
                            if (conn) {
                                // Start the header timeout immediately so that clients which
                                // connect without sending a request are also reaped.
                                conn->set_state(aevrix::ConnectionState::Reading);
                                conn->set_read_state(aevrix::ReadState::Headers);
                                // Phase 24: the header deadline comes from the
                                // current configuration generation.
                                conn->set_deadline(*config_store.snapshot());

                                // Add connection to event loop for EPOLLIN
                                int client_fd_value = client_fd.value();
                                event_loop.add_fd(client_fd_value, EPOLLIN,
                                    [client_fd_value, &connection_manager, &router, &event_loop, &worker_pool, &completion_handler, &proxy_handler, &config_store
#ifdef AEVRIX_ENABLE_TLS
                                     , &tls_context
#endif
                                    ]([[maybe_unused]] int, aevrix::EventType client_event) {
                                        auto conn_ptr = connection_manager.get_connection(client_fd_value);
                                        if (!conn_ptr) {
                                            // Connection already removed, clean up event loop
                                            event_loop.remove_fd(client_fd_value);
                                            return;
                                        }

                                        // Phase 24: one snapshot per event, so both
                                        // branches below see the same generation.
                                        const std::shared_ptr<const aevrix::ServerConfig> live_config =
                                            config_store.snapshot();
#ifdef AEVRIX_ENABLE_TLS
                                        // Phase 21: Handle TLS handshake if enabled
                                        if (conn_ptr->is_tls_enabled() && conn_ptr->tls_connection() &&
                                            conn_ptr->state() == aevrix::ConnectionState::TlsHandshake) {
                                            if (client_event == aevrix::EventType::Readable || client_event == aevrix::EventType::Writable) {
                                                auto io_req = conn_ptr->tls_connection()->do_handshake();

                                                if (conn_ptr->tls_connection()->is_handshake_complete()) {
                                                    // Handshake complete, transition to HTTP read
                                                    conn_ptr->set_state(aevrix::ConnectionState::Reading);
                                                    conn_ptr->set_read_state(aevrix::ReadState::Headers);
                                                    event_loop.modify_fd(client_fd_value, EPOLLIN);
                                                    aevrix::g_logger.log_with_connection(aevrix::LogLevel::INFO, conn_ptr->id(), "TLS handshake complete, entering HTTP read state");
                                                } else if (conn_ptr->tls_connection()->is_handshake_failed()) {
                                                    // Handshake failed, close connection
                                                    event_loop.remove_fd(client_fd_value);
                                                    connection_manager.remove_connection(client_fd_value);
                                                    aevrix::g_logger.log_with_connection(aevrix::LogLevel::WARN, conn_ptr->id(), "TLS handshake failed, closing connection");
                                                    return;
                                                } else {
                                                    // Handshake in progress, update epoll interest
                                                    uint32_t events = EPOLLIN;
                                                    if (io_req == aevrix::TlsIoRequirement::WantWrite) {
                                                        events = EPOLLOUT;
                                                    } else if (io_req == aevrix::TlsIoRequirement::WantRead) {
                                                        events = EPOLLIN;
                                                    } else if (io_req == aevrix::TlsIoRequirement::Both) {
                                                        events = EPOLLIN | EPOLLOUT;
                                                    }
                                                    event_loop.modify_fd(client_fd_value, events);
                                                    return;
                                                }
                                            }
                                        }
#endif

                                        if (client_event == aevrix::EventType::Readable) {
                                            bool keep_alive = handle_read_event(conn_ptr, router, event_loop, worker_pool, completion_handler,
                                                               proxy_handler, *live_config);
                                            if (!keep_alive) {
                                                // Remove from event loop first (prevents further events)
                                                event_loop.remove_fd(client_fd_value);
                                                // Then remove from connection manager (may destroy connection)
                                                connection_manager.remove_connection(client_fd_value);
                                            }
                                        } else if (client_event == aevrix::EventType::Writable) {
                                            // Stage 4: Handle EPOLLOUT event
                                            bool keep_alive = handle_write_event(conn_ptr, event_loop, *live_config);
                                            if (!keep_alive) {
                                                event_loop.remove_fd(client_fd_value);
                                                connection_manager.remove_connection(client_fd_value);
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

#ifdef AEVRIX_ENABLE_TLS
            // Phase 21: Add TLS listener to event loop if TLS is enabled
            if (tls_listener && tls_context) {
                tls_listener->stop();  // Stop current blocking listener
                if (!tls_listener->start(config.host(), config.tls_port(), true)) {  // Start with non-blocking
                    aevrix::g_logger.error("Failed to start non-blocking TLS listener");
                    return 1;
                }

                if (!event_loop.add_fd(tls_listener->get_socket(), EPOLLIN,
                    [&tls_listener, &connection_manager, &router, &event_loop, &worker_pool, &completion_handler, &proxy_handler, &config_store, &connection_count, &tls_context]([[maybe_unused]] int fd, aevrix::EventType event) {
                        if (event == aevrix::EventType::Readable) {
                            // Accept new TLS connection
                            auto client_fd = tls_listener->accept();
                            if (client_fd.has_value()) {
                                // Register connection with ConnectionManager
                                auto conn = connection_manager.register_connection(client_fd.value());
                                if (conn) {
                                    // Initialize TLS for this connection
                                    try {
                                        conn->init_tls(*tls_context);
                                        conn->set_state(aevrix::ConnectionState::TlsHandshake);
                                        // Phase 24: handshake deadline from the
                                        // current configuration generation.
                                        conn->set_deadline(*config_store.snapshot());
                                    } catch (const std::exception& e) {
                                        aevrix::g_logger.log_with_connection(aevrix::LogLevel::ERR, conn->id(), "Failed to initialize TLS: " + std::string(e.what()));
                                        event_loop.remove_fd(client_fd.value());
                                        connection_manager.remove_connection(client_fd.value());
                                        return;
                                    }

                                    // Add connection to event loop for EPOLLIN (TLS handshake starts with read)
                                    int client_fd_value = client_fd.value();
                                    event_loop.add_fd(client_fd_value, EPOLLIN,
                                        [client_fd_value, &connection_manager, &router, &event_loop, &worker_pool, &completion_handler, &proxy_handler, &config_store, &tls_context]([[maybe_unused]] int, aevrix::EventType client_event) {
                                            auto conn_ptr = connection_manager.get_connection(client_fd_value);
                                            if (!conn_ptr) {
                                                // Connection already removed, clean up event loop
                                                event_loop.remove_fd(client_fd_value);
                                                return;
                                            }

                                            // Phase 24: one snapshot per event, so every
                                            // branch below sees the same generation.
                                            const std::shared_ptr<const aevrix::ServerConfig> live_config =
                                                config_store.snapshot();
                                            // Phase 21: Handle TLS handshake
                                            if (conn_ptr->is_tls_enabled() && conn_ptr->tls_connection() &&
                                                conn_ptr->state() == aevrix::ConnectionState::TlsHandshake) {
                                                if (client_event == aevrix::EventType::Readable || client_event == aevrix::EventType::Writable) {
                                                    auto io_req = conn_ptr->tls_connection()->do_handshake();

                                                    if (conn_ptr->tls_connection()->is_handshake_complete()) {
                                                        // Handshake complete, transition to HTTP read
                                                        conn_ptr->set_state(aevrix::ConnectionState::Reading);
                                                        conn_ptr->set_read_state(aevrix::ReadState::Headers);
                                                        event_loop.modify_fd(client_fd_value, EPOLLIN);
                                                        aevrix::g_logger.log_with_connection(aevrix::LogLevel::INFO, conn_ptr->id(), "TLS handshake complete, entering HTTP read state");
                                                    } else if (conn_ptr->tls_connection()->is_handshake_failed()) {
                                                        // Handshake failed, close connection
                                                        event_loop.remove_fd(client_fd_value);
                                                        connection_manager.remove_connection(client_fd_value);
                                                        aevrix::g_logger.log_with_connection(aevrix::LogLevel::WARN, conn_ptr->id(), "TLS handshake failed, closing connection");
                                                        return;
                                                    } else {
                                                        // Handshake in progress, update epoll interest
                                                        uint32_t events = EPOLLIN;
                                                        if (io_req == aevrix::TlsIoRequirement::WantWrite) {
                                                            events = EPOLLOUT;
                                                        } else if (io_req == aevrix::TlsIoRequirement::WantRead) {
                                                            events = EPOLLIN;
                                                        } else if (io_req == aevrix::TlsIoRequirement::Both) {
                                                            events = EPOLLIN | EPOLLOUT;
                                                        }
                                                        event_loop.modify_fd(client_fd_value, events);
                                                        return;
                                                    }
                                                }
                                            }

                                            // After handshake, use existing HTTP handlers
                                            if (client_event == aevrix::EventType::Readable) {
                                                bool keep_alive = handle_read_event(conn_ptr, router, event_loop, worker_pool, completion_handler,
                                                               proxy_handler, *live_config);
                                                if (!keep_alive) {
                                                    event_loop.remove_fd(client_fd_value);
                                                    connection_manager.remove_connection(client_fd_value);
                                                }
                                            } else if (client_event == aevrix::EventType::Writable) {
                                                bool keep_alive = handle_write_event(conn_ptr, event_loop, *live_config);
                                                if (!keep_alive) {
                                                    event_loop.remove_fd(client_fd_value);
                                                    connection_manager.remove_connection(client_fd_value);
                                                }
                                            } else if (client_event == aevrix::EventType::Error || client_event == aevrix::EventType::Hangup) {
                                                event_loop.remove_fd(client_fd_value);
                                                connection_manager.remove_connection(client_fd_value);
                                            }
                                        });

                                    connection_count++;
                                    aevrix::g_logger.info("Total connections handled: " + std::to_string(connection_count));
                                } else {
                                    aevrix::g_logger.warn("TLS connection rejected (at capacity)");
                                }
                            }
                        }
                    })) {
                    aevrix::g_logger.error("Failed to add TLS listener to event loop");
                    return 1;
                }
            }
#endif

            aevrix::g_logger.info("Starting event loop...");
            while (!aevrix::g_signal_handler.shutdown_requested()) {
                // Run event loop with 1 second timeout for periodic timeout sweeping
                if (!event_loop.run(1000)) {
                    break;
                }

                // Stage 6: Sweep timed-out connections after each timeout
                size_t timed_out = connection_manager.sweep_timeouts();
                if (timed_out > 0) {
                    aevrix::g_logger.info("Swept " + std::to_string(timed_out) + " timed-out connections");
                }

                // Phase 22: enforce upstream deadlines and evict idle pooled
                // upstream connections that have been unused for too long.
                proxy_handler.sweep_timeouts();

                // Phase 24: fallback for reload requests when the eventfd path
                // is unavailable (and a safety net if a wake-up were ever lost).
                // consume_reload_request() guarantees one reload per SIGHUP.
                if (aevrix::g_signal_handler.consume_reload_request() && perform_reload) {
                    perform_reload("");
                }

                // Phase 23: queue server keepalive Pings whose interval has
                // elapsed and arm EPOLLOUT so they flush promptly.
                // Phase 24: the interval comes from the live snapshot, so a
                // reload changes it without a restart.
                const std::shared_ptr<const aevrix::ServerConfig> live =
                    config_store.snapshot();
                if (live->websocket_ping_interval_ms() > 0) {
                    for (int fd : connection_manager.maintain_websockets(
                             live->websocket_ping_interval_ms())) {
                        event_loop.modify_fd(fd, EPOLLIN | EPOLLOUT);
                    }
                }
            }

            // Phase 23: offer WebSocket peers a Close(1001 going away) before
            // the sockets are torn down.
            const size_t ws_notified = connection_manager.initiate_websocket_shutdown(1001);
            if (ws_notified > 0) {
                aevrix::g_logger.info("Queued WebSocket close frames for " +
                                      std::to_string(ws_notified) + " connection(s)");
            }

            // Phase 22: abandon in-flight proxy requests and close pooled
            // upstream connections before the event loop is destroyed.
            proxy_handler.shutdown();
            
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


