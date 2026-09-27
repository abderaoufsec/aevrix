// =============================================================================
// Aevrix - Observability Endpoints
// =============================================================================
// This header provides HTTP endpoints for server observability.
// Endpoints:
// - /metrics: Exposes metrics in Prometheus-compatible format
// - /health: Health check endpoint
// - /server-info: Server information endpoint
//
// Phase 20 Implementation:
// - Metrics endpoint for Prometheus-style metrics
// - Health check endpoint for uptime monitoring
// - Server info endpoint for version and configuration
// =============================================================================

#pragma once

#include "aevrix/metrics.h"
#include "aevrix/http_response.h"
#include "aevrix/http_request.h"
#include "aevrix/router.h"
#include <string>

namespace aevrix {
namespace observability {

// =============================================================================
// Metrics Endpoint
// =============================================================================
// Returns server metrics in Prometheus-compatible format.
// =============================================================================

/**
 * @brief Handle /metrics endpoint
 * 
 * Returns metrics in Prometheus text format for scraping by monitoring systems.
 * 
 * @param request The HTTP request (unused but required for signature)
 * @return http::HttpResponse HTTP response with metrics in Prometheus format
 */
http::HttpResponse handle_metrics_endpoint(const http::HttpRequest& request);

// =============================================================================
// Health Endpoint
// =============================================================================
// Returns server health status for uptime monitoring.
// =============================================================================

/**
 * @brief Handle /health endpoint
 * 
 * Returns server health status. Can be extended to check dependencies
 * (database, cache, etc.) in the future.
 * 
 * @param request The HTTP request (unused but required for signature)
 * @return http::HttpResponse HTTP response with health status
 */
http::HttpResponse handle_health_endpoint(const http::HttpRequest& request);

// =============================================================================
// Server Info Endpoint
// =============================================================================
// Returns server information including version and configuration.
// =============================================================================

/**
 * @brief Handle /server-info endpoint
 * 
 * Returns server information including version, build type, and configuration.
 * 
 * @param request The HTTP request (unused but required for signature)
 * @return http::HttpResponse HTTP response with server information
 */
http::HttpResponse handle_server_info_endpoint(const http::HttpRequest& request);

// =============================================================================
// Register Observability Endpoints
// =============================================================================
// Registers observability endpoints with the router.
// =============================================================================

/**
 * @brief Register observability endpoints with the router
 * 
 * This function registers the /metrics, /health, and /server-info endpoints
 * with the provided router.
 * 
 * @param router The router to register endpoints with
 */
void register_observability_endpoints(Router& router);

} // namespace observability
} // namespace aevrix
