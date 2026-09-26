// =============================================================================
// Aevrix - Router
// =============================================================================
// This file implements a lightweight request router for application-level routing.
// In Phase 12, we introduce routing without turning Aevrix into a framework.
//
// The router:
// - Consumes Request objects (no socket knowledge)
// - Produces Handler/Response pairs
// - Supports GET /, GET /health, GET /metrics
// - Simple path-based matching
// - Method-based routing
//
// Design rule: Routing must not know about sockets
//
// Previous Phases:
// - Phase 11: Worker pool for blocking operations
//
// Future Phases Will Add:
// - Phase 13: Configuration system
// =============================================================================

#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include "aevrix/http_request.h"
#include "aevrix/http_response.h"

namespace aevrix {

// Use http namespace for readability
using http::HttpRequest;
using http::HttpResponse;
using http::ConnectionPolicy;
using http::StatusCode;

/**
 * @brief Route handler function type
 * 
 * A route handler takes an HTTP request and returns an HTTP response.
// Handlers are pure functions that consume Request and produce Response,
// following the design rule that routing must not know about sockets.
 * 
 * @param request The HTTP request
 * @return HttpResponse The HTTP response
 */
using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;

/**
 * @brief Lightweight request router
 * 
 * A simple router that matches HTTP requests to handlers based on
// method and path. The router is lightweight and does not turn Aevrix
// into a framework - it's just a simple mapping from (method, path) to handler.
 * 
 * Design rule: Routing must not know about sockets.
// The router only consumes Request objects and produces Response objects.
 * 
 * Initial routes:
// - GET /: Root endpoint
// - GET /health: Health check endpoint
// - GET /metrics: Metrics endpoint
 * 
 * Future routes (not implemented yet):
// - GET /users/:id: User detail endpoint
// - POST /users: User creation endpoint
 */
class Router {
public:
    /**
     * @brief Construct a router
     */
    Router();

    /**
     * @brief Destructor
     */
    ~Router() = default;

    // Delete copy operations
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;

    /**
     * @brief Register a route handler
     * 
     * Registers a handler for a specific method and path.
     * 
     * @param method The HTTP method (e.g., "GET", "POST")
     * @param path The URL path (e.g., "/", "/health")
     * @param handler The handler function
     */
    void add_route(const std::string& method, const std::string& path, RouteHandler handler);

    /**
     * @brief Route a request to a handler
     * 
     * Matches the request's method and path to a registered handler
// and executes it to produce a response.
     * 
     * @param request The HTTP request
     * @return HttpResponse The HTTP response from the handler
     * @throws std::runtime_error if no handler is found
     */
    HttpResponse route(const HttpRequest& request) const;

    /**
     * @brief Check if a route exists
     * 
     * @param method The HTTP method
     * @param path The URL path
     * @return true if a handler is registered, false otherwise
     */
    bool has_route(const std::string& method, const std::string& path) const;

private:
    /**
     * @brief Generate a route key
     * 
     * Combines method and path into a single key for the route map.
     * 
     * @param method The HTTP method
     * @param path The URL path
     * @return std::string The route key
     */
    std::string make_route_key(const std::string& method, const std::string& path) const;

    // Route map: key (METHOD:PATH) -> handler
    std::unordered_map<std::string, RouteHandler> routes_;
};

} // namespace aevrix
