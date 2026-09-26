// =============================================================================
// Aevrix - Router Implementation
// =============================================================================
// This file implements the router for application-level routing.
// =============================================================================

#include "aevrix/router.h"
#include "aevrix/http_method.h"
#include "aevrix/http_status.h"
#include <iostream>
#include <algorithm>

namespace aevrix {

Router::Router() {
    std::cout << "Router initialized\n";
}

void Router::add_route(const std::string& method, const std::string& path, RouteHandler handler) {
    std::string key = make_route_key(method, path);
    routes_[key] = std::move(handler);
    std::cout << "Registered route: " << method << " " << path << "\n";
}

HttpResponse Router::route(const HttpRequest& request) const {
    // Convert HttpMethod enum to string
    std::string method_str = http::http_method_to_string(request.method());
    std::string key = make_route_key(method_str, request.target());
    
    auto it = routes_.find(key);
    if (it != routes_.end()) {
        // Found a handler, execute it
        return it->second(request);
    }
    
    // No handler found, return 404
    std::cout << "No handler found for: " << method_str << " " << request.target() << "\n";
    HttpResponse response(StatusCode::NotFound, "Not Found");
    response.set_header("Content-Type", "text/plain");
    response.set_header("Server", "Aevrix/0.1.0");
    response.set_connection_policy(ConnectionPolicy::Close);
    return response;
}

bool Router::has_route(const std::string& method, const std::string& path) const {
    std::string key = make_route_key(method, path);
    return routes_.find(key) != routes_.end();
}

std::string Router::make_route_key(const std::string& method, const std::string& path) const {
    // Convert method to uppercase for case-insensitive matching
    std::string upper_method = method;
    std::transform(upper_method.begin(), upper_method.end(), upper_method.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return upper_method + ":" + path;
}

} // namespace aevrix
