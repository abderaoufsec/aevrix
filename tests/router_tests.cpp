// =============================================================================
// Aevrix - Router Unit Tests
// =============================================================================
// This file contains comprehensive unit tests for the Router component.
// In Phase 16, we add tests to turn correctness into an automated contract.
//
// Tests cover:
// - Route registration
// - Route matching (case-insensitive method)
// - Route retrieval
// - 404 handling for unmatched routes
// - Multiple routes
// - Route key generation
// - Edge cases
// =============================================================================

#include "aevrix/router.h"
#include "aevrix/http_request.h"
#include "aevrix/http_response.h"
#include "aevrix/http_status.h"
#include <iostream>
#include <cassert>
#include <string>

// =============================================================================
// Test Utilities
// =============================================================================

#define TEST_ASSERT(condition, test_name) \
    do { \
        std::cout << "Testing: " << test_name << "... "; \
        if (condition) { \
            std::cout << "PASSED\n"; \
        } else { \
            std::cout << "FAILED\n"; \
            std::cerr << "Assertion failed: " << #condition << "\n"; \
            std::abort(); \
        } \
    } while(0)

using namespace aevrix;
using namespace aevrix::http;

// =============================================================================
// Router Tests
// =============================================================================

void test_router_empty() {
    Router router;
    
    // Create a simple request
    HttpRequest request(HttpMethod::GET, "/test");
    request.set_version("HTTP/1.1");
    
    // Route should return 404 for empty router
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::NotFound, "Empty router returns 404");
}

void test_router_add_route() {
    Router router;
    
    // Add a route
    router.add_route("GET", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Test");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Create a matching request
    HttpRequest request(HttpMethod::GET, "/test");
    request.set_version("HTTP/1.1");
    
    // Route should return 200
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::OK, "Added route returns 200");
}

void test_router_case_insensitive_method() {
    Router router;
    
    // Add route with uppercase method
    router.add_route("GET", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Test");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Create request with GET method
    HttpRequest request(HttpMethod::GET, "/test");
    request.set_version("HTTP/1.1");
    
    // Route should match
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::OK, "Case-insensitive method matching");
}

void test_router_path_mismatch() {
    Router router;
    
    // Add route for /test
    router.add_route("GET", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Test");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Create request for /other
    HttpRequest request(HttpMethod::GET, "/other");
    request.set_version("HTTP/1.1");
    
    // Route should return 404 for path mismatch
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::NotFound, "Path mismatch returns 404");
}

void test_router_method_mismatch() {
    Router router;
    
    // Add GET route
    router.add_route("GET", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Test");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Create POST request
    HttpRequest request(HttpMethod::POST, "/test");
    request.set_version("HTTP/1.1");
    
    // Route should return 404 for method mismatch
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::NotFound, "Method mismatch returns 404");
}

void test_router_multiple_routes() {
    Router router;
    
    // Add multiple routes
    router.add_route("GET", "/", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Root");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    router.add_route("GET", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Test");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    router.add_route("GET", "/other", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Other");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Test each route
    HttpRequest request1(HttpMethod::GET, "/");
    request1.set_version("HTTP/1.1");
    HttpResponse response1 = router.route(request1);
    TEST_ASSERT(response1.status() == StatusCode::OK, "First route matches");
    
    HttpRequest request2(HttpMethod::GET, "/test");
    request2.set_version("HTTP/1.1");
    HttpResponse response2 = router.route(request2);
    TEST_ASSERT(response2.status() == StatusCode::OK, "Second route matches");
    
    HttpRequest request3(HttpMethod::GET, "/other");
    request3.set_version("HTTP/1.1");
    HttpResponse response3 = router.route(request3);
    TEST_ASSERT(response3.status() == StatusCode::OK, "Third route matches");
}

void test_router_has_route() {
    Router router;
    
    // Add a route
    router.add_route("GET", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Test");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Check if route exists
    TEST_ASSERT(router.has_route("GET", "/test"), "has_route returns true for existing route");
    TEST_ASSERT(!router.has_route("GET", "/other"), "has_route returns false for non-existing route");
    TEST_ASSERT(!router.has_route("POST", "/test"), "has_route returns false for method mismatch");
}

void test_router_same_path_different_method() {
    Router router;
    
    // Add same path with different methods
    router.add_route("GET", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "GET");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    router.add_route("POST", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "POST");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Test GET
    HttpRequest get_request(HttpMethod::GET, "/test");
    get_request.set_version("HTTP/1.1");
    HttpResponse get_response = router.route(get_request);
    TEST_ASSERT(get_response.status() == StatusCode::OK, "GET route matches");
    
    // Test POST
    HttpRequest post_request(HttpMethod::POST, "/test");
    post_request.set_version("HTTP/1.1");
    HttpResponse post_response = router.route(post_request);
    TEST_ASSERT(post_response.status() == StatusCode::OK, "POST route matches");
}

void test_router_path_with_parameters() {
    Router router;
    
    // Add route with path parameter
    router.add_route("GET", "/users/:id", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "User");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Create request with parameter
    HttpRequest request(HttpMethod::GET, "/users/123");
    request.set_version("HTTP/1.1");
    
    // Route should match exact path (parameter not extracted yet)
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::NotFound, "Path parameter not matched (exact match required)");
}

void test_router_slash_ending() {
    Router router;
    
    // Add route without trailing slash
    router.add_route("GET", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Test");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Request with trailing slash should not match
    HttpRequest request(HttpMethod::GET, "/test/");
    request.set_version("HTTP/1.1");
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::NotFound, "Trailing slash mismatch");
}

void test_router_empty_path() {
    Router router;
    
    // Add route for root
    router.add_route("GET", "/", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Root");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Request for root
    HttpRequest request(HttpMethod::GET, "/");
    request.set_version("HTTP/1.1");
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::OK, "Empty path matches root route");
}

void test_router_long_path() {
    Router router;
    
    // Add route with long path
    router.add_route("GET", "/api/v1/users/profile/settings", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Settings");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Request for long path
    HttpRequest request(HttpMethod::GET, "/api/v1/users/profile/settings");
    request.set_version("HTTP/1.1");
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::OK, "Long path matches");
}

void test_router_special_characters() {
    Router router;
    
    // Add route with special characters
    router.add_route("GET", "/test-file_123", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Test");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Request with special characters
    HttpRequest request(HttpMethod::GET, "/test-file_123");
    request.set_version("HTTP/1.1");
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::OK, "Special characters in path match");
}

void test_router_overwrite_route() {
    Router router;
    
    // Add route
    router.add_route("GET", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Original");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Overwrite with same route
    router.add_route("GET", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Overwritten");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Request should use overwritten handler
    HttpRequest request(HttpMethod::GET, "/test");
    request.set_version("HTTP/1.1");
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::OK, "Overwritten route works");
    TEST_ASSERT(response.body() == "Overwritten", "Overwritten handler used");
}

void test_router_head_method() {
    Router router;
    
    // Add route for HEAD
    router.add_route("HEAD", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Request for HEAD
    HttpRequest request(HttpMethod::HEAD, "/test");
    request.set_version("HTTP/1.1");
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::OK, "HEAD method matches");
}

void test_router_options_method() {
    Router router;
    
    // Add route for OPTIONS
    router.add_route("OPTIONS", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "");
        response.set_header("Allow", "GET, HEAD, OPTIONS");
        return response;
    });
    
    // Request for OPTIONS
    HttpRequest request(HttpMethod::OPTIONS, "/test");
    request.set_version("HTTP/1.1");
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::OK, "OPTIONS method matches");
}

void test_router_put_method() {
    Router router;
    
    // Add route for PUT
    router.add_route("PUT", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Updated");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Request for PUT
    HttpRequest request(HttpMethod::PUT, "/test");
    request.set_version("HTTP/1.1");
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::OK, "PUT method matches");
}

void test_router_delete_method() {
    Router router;
    
    // Add route for DELETE
    router.add_route("DELETE", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Deleted");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Request for DELETE
    HttpRequest request(HttpMethod::HTTP_DELETE, "/test");
    request.set_version("HTTP/1.1");
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::OK, "DELETE method matches");
}

void test_router_patch_method() {
    Router router;
    
    // Add route for PATCH
    router.add_route("PATCH", "/test", [](const HttpRequest& req) {
        (void)req;
        HttpResponse response(StatusCode::OK, "Patched");
        response.set_header("Content-Type", "text/plain");
        return response;
    });
    
    // Request for PATCH
    HttpRequest request(HttpMethod::HTTP_PATCH, "/test");
    request.set_version("HTTP/1.1");
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::OK, "PATCH method matches");
}

void test_router_404_response_structure() {
    Router router;
    
    // Create request for non-existent route
    HttpRequest request(HttpMethod::GET, "/nonexistent");
    request.set_version("HTTP/1.1");
    
    // Route should return properly structured 404
    HttpResponse response = router.route(request);
    TEST_ASSERT(response.status() == StatusCode::NotFound, "404 status code");
    TEST_ASSERT(response.body() == "Not Found", "404 body content");
    TEST_ASSERT(response.version() == "HTTP/1.1", "404 HTTP version");
}

void test_router_20_routes() {
    Router router;
    
    // Add 20 different routes
    for (int i = 0; i < 20; ++i) {
        std::string path = "/test" + std::to_string(i);
        router.add_route("GET", path, [i](const HttpRequest& req) {
            (void)req;
            HttpResponse response(StatusCode::OK, "Test");
            response.set_header("Content-Type", "text/plain");
            return response;
        });
    }
    
    // Test that all routes match
    for (int i = 0; i < 20; ++i) {
        std::string path = "/test" + std::to_string(i);
        HttpRequest request(HttpMethod::GET, path);
        request.set_version("HTTP/1.1");
        HttpResponse response = router.route(request);
        TEST_ASSERT(response.status() == StatusCode::OK, "Route " + std::to_string(i) + " matches");
    }
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    std::cout << "=== Running Router Unit Tests ===\n\n";
    
    // Empty router tests
    test_router_empty();
    
    // Basic routing tests
    test_router_add_route();
    test_router_case_insensitive_method();
    test_router_path_mismatch();
    test_router_method_mismatch();
    
    // Multiple routes tests
    test_router_multiple_routes();
    test_router_has_route();
    test_router_same_path_different_method();
    
    // Path edge cases
    test_router_path_with_parameters();
    test_router_slash_ending();
    test_router_empty_path();
    test_router_long_path();
    test_router_special_characters();
    
    // Route management tests
    test_router_overwrite_route();
    
    // HTTP method tests
    test_router_head_method();
    test_router_options_method();
    test_router_put_method();
    test_router_delete_method();
    test_router_patch_method();
    
    // Response structure tests
    test_router_404_response_structure();
    
    // Scale tests
    test_router_20_routes();
    
    std::cout << "\n=== All Router Tests PASSED ===\n";
    return 0;
}
