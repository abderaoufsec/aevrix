// =============================================================================
// Aevrix - Observability Endpoints Implementation
// =============================================================================
// This file implements the observability endpoints for server monitoring.
// =============================================================================

#include "aevrix/observability.h"
#include "aevrix/router.h"
#include "aevrix/http_request.h"
#include "aevrix/http_response.h"
#include <sstream>

namespace aevrix {
namespace observability {

// =============================================================================
// Metrics Endpoint Implementation
// =============================================================================

http::HttpResponse handle_metrics_endpoint(const http::HttpRequest& request) {
    // Only allow GET requests
    if (request.method() != http::HttpMethod::GET) {
        return http::HttpResponse(http::StatusCode::MethodNotAllowed);
    }
    
    // Get metrics in Prometheus format
    std::string metrics = g_metrics.export_prometheus();
    
    // Build response
    http::HttpResponse response(http::StatusCode::OK);
    response.set_body(metrics);
    response.set_header("Content-Type", "text/plain; version=0.0.4");
    
    return response;
}

// =============================================================================
// Health Endpoint Implementation
// =============================================================================

http::HttpResponse handle_health_endpoint(const http::HttpRequest& request) {
    // Only allow GET requests
    if (request.method() != http::HttpMethod::GET) {
        return http::HttpResponse(http::StatusCode::MethodNotAllowed);
    }
    
    // Build health check response
    std::stringstream ss;
    ss << "OK\n";
    ss << "status: healthy\n";
    ss << "active_connections: " << g_metrics.active_connections() << "\n";
    
    http::HttpResponse response(http::StatusCode::OK);
    response.set_body(ss.str());
    response.set_header("Content-Type", "text/plain");
    
    return response;
}

// =============================================================================
// Server Info Endpoint Implementation
// =============================================================================

http::HttpResponse handle_server_info_endpoint(const http::HttpRequest& request) {
    // Only allow GET requests
    if (request.method() != http::HttpMethod::GET) {
        return http::HttpResponse(http::StatusCode::MethodNotAllowed);
    }
    
    // Build server info response
    std::stringstream ss;
    ss << "Aevrix HTTP Server\n";
    ss << "Version: 0.1.0\n";
    ss << "Phase: 20 - Observability\n";
    ss << "HTTP Version: HTTP/1.1\n";
    ss << "C++ Standard: C++20\n";
    ss << "\n";
    ss << "Metrics:\n";
    ss << "  Total Requests: " << g_metrics.total_requests() << "\n";
    ss << "  Total Connections: " << g_metrics.total_connections() << "\n";
    ss << "  Active Connections: " << g_metrics.active_connections() << "\n";
    ss << "  Total Errors: " << g_metrics.total_errors() << "\n";
    ss << "  Bytes Sent: " << g_metrics.bytes_sent() << "\n";
    ss << "  Bytes Received: " << g_metrics.bytes_received() << "\n";
    ss << "  Parser Failures: " << g_metrics.parser_failures() << "\n";
    
    http::HttpResponse response(http::StatusCode::OK);
    response.set_body(ss.str());
    response.set_header("Content-Type", "text/plain");
    
    return response;
}

// =============================================================================
// Register Observability Endpoints Implementation
// =============================================================================

void register_observability_endpoints(Router& router) {
    // Register /metrics endpoint
    router.add_route("GET", "/metrics", handle_metrics_endpoint);
    
    // Register /health endpoint
    router.add_route("GET", "/health", handle_health_endpoint);
    
    // Register /server-info endpoint
    router.add_route("GET", "/server-info", handle_server_info_endpoint);
}

} // namespace observability
} // namespace aevrix
