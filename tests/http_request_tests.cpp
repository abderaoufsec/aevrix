// =============================================================================
// Aevrix - HTTP Request Unit Tests
// =============================================================================
// This file contains comprehensive unit tests for HTTP request components:
// - HttpMethod enum and utilities
// - HttpRequest class
// - HttpRequestParser class
//
// Tests cover:
// - HTTP method conversion and categorization
// - Request construction and manipulation
// - Parser state machine
// - Request line parsing
// - Header parsing with validation
// - Body parsing
// - Incremental parsing
// - Error handling
// - Limit enforcement
// =============================================================================

#include "aevrix/http_method.h"
#include "aevrix/http_request.h"
#include "aevrix/http_request_parser.h"
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

// =============================================================================
// HttpMethod Tests
// =============================================================================

void test_http_method_conversion() {
    using namespace aevrix::http;
    
    TEST_ASSERT(http_method_to_string(HttpMethod::GET) == "GET", "GET converts to 'GET'");
    TEST_ASSERT(http_method_to_string(HttpMethod::POST) == "POST", "POST converts to 'POST'");
    TEST_ASSERT(http_method_to_string(HttpMethod::HTTP_DELETE) == "DELETE", "DELETE converts to 'DELETE'");
}

void test_string_to_http_method() {
    using namespace aevrix::http;
    
    TEST_ASSERT(string_to_http_method("GET") == HttpMethod::GET, "GET string converts");
    TEST_ASSERT(string_to_http_method("get") == HttpMethod::GET, "get string converts (case-insensitive)");
    TEST_ASSERT(string_to_http_method("POST") == HttpMethod::POST, "POST string converts");
    TEST_ASSERT(string_to_http_method("INVALID") == HttpMethod::HTTP_UNKNOWN, "Invalid string converts to UNKNOWN");
}

void test_method_categorization() {
    using namespace aevrix::http;
    
    TEST_ASSERT(is_safe_method(HttpMethod::GET), "GET is safe");
    TEST_ASSERT(is_safe_method(HttpMethod::HEAD), "HEAD is safe");
    TEST_ASSERT(!is_safe_method(HttpMethod::POST), "POST is not safe");
    
    TEST_ASSERT(is_idempotent_method(HttpMethod::GET), "GET is idempotent");
    TEST_ASSERT(is_idempotent_method(HttpMethod::PUT), "PUT is idempotent");
    TEST_ASSERT(is_idempotent_method(HttpMethod::HTTP_DELETE), "DELETE is idempotent");
    TEST_ASSERT(!is_idempotent_method(HttpMethod::POST), "POST is not idempotent");
    
    TEST_ASSERT(method_requires_body(HttpMethod::POST), "POST requires body");
    TEST_ASSERT(method_requires_body(HttpMethod::PUT), "PUT requires body");
    TEST_ASSERT(!method_requires_body(HttpMethod::GET), "GET does not require body");
}

// =============================================================================
// HttpRequest Tests
// =============================================================================

void test_request_default_construction() {
    using namespace aevrix::http;
    
    HttpRequest request;
    TEST_ASSERT(request.method() == HttpMethod::GET, "Default method is GET");
    TEST_ASSERT(request.target() == "/", "Default target is /");
    TEST_ASSERT(request.version() == "HTTP/1.1", "Default version is HTTP/1.1");
    TEST_ASSERT(!request.has_body(), "Default request has no body");
}

void test_request_with_method_target() {
    using namespace aevrix::http;
    
    HttpRequest request(HttpMethod::POST, "/api/users");
    TEST_ASSERT(request.method() == HttpMethod::POST, "Method set correctly");
    TEST_ASSERT(request.target() == "/api/users", "Target set correctly");
}

void test_request_with_body() {
    using namespace aevrix::http;
    
    HttpRequest request(HttpMethod::POST, "/api/users", "{\"name\":\"test\"}");
    TEST_ASSERT(request.has_body(), "Request has body");
    TEST_ASSERT(request.body() == "{\"name\":\"test\"}", "Body content correct");
    TEST_ASSERT(request.content_length() == 16, "Content length correct");
}

void test_request_headers() {
    using namespace aevrix::http;
    
    HttpRequest request;
    request.set_header("Content-Type", "application/json");
    request.set_header("Content-Length", "100");
    
    TEST_ASSERT(request.headers().size() == 2, "Headers count correct");
    TEST_ASSERT(request.get_header("Content-Type") == "application/json", "Header value correct");
}

void test_request_validation() {
    using namespace aevrix::http;
    
    HttpRequest request(HttpMethod::GET, "/index.html");
    TEST_ASSERT(request.is_valid(), "Valid request");
    
    // Invalid: empty target
    HttpRequest invalid_target(HttpMethod::GET, "");
    TEST_ASSERT(!invalid_target.is_valid(), "Invalid request with empty target");
}

void test_request_line() {
    using namespace aevrix::http;
    
    HttpRequest request(HttpMethod::GET, "/index.html");
    std::string req_line = request.request_line();
    
    TEST_ASSERT(req_line == "GET /index.html HTTP/1.1", "Request line format correct");
}

// =============================================================================
// HttpRequestParser Tests
// =============================================================================

void test_parser_default_construction() {
    using namespace aevrix::http;
    
    HttpRequestParser parser;
    TEST_ASSERT(parser.state() == ParserState::RequestLine, "Initial state is RequestLine");
    TEST_ASSERT(!parser.is_complete(), "Parser not complete initially");
    TEST_ASSERT(!parser.has_error(), "Parser not in error initially");
}

void test_parser_simple_get_request() {
    using namespace aevrix::http;
    
    HttpRequestParser parser;
    std::string request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    
    parser.feed(request);
    
    TEST_ASSERT(parser.is_complete(), "Parser completed");
    TEST_ASSERT(!parser.has_error(), "No error occurred");
    
    const HttpRequest& parsed = parser.request();
    TEST_ASSERT(parsed.method() == HttpMethod::GET, "Method parsed correctly");
    TEST_ASSERT(parsed.target() == "/", "Target parsed correctly");
    TEST_ASSERT(parsed.version() == "HTTP/1.1", "Version parsed correctly");
    TEST_ASSERT(parsed.get_header("Host") == "example.com", "Header parsed correctly");
}

void test_parser_post_request_with_body() {
    using namespace aevrix::http;
    
    HttpRequestParser parser;
    std::string request = "POST /api/users HTTP/1.1\r\n"
                         "Content-Type: application/json\r\n"
                         "Content-Length: 16\r\n"
                         "\r\n"
                         "{\"name\":\"test\"}";
    
    parser.feed(request);
    
    TEST_ASSERT(parser.is_complete(), "Parser completed");
    TEST_ASSERT(!parser.has_error(), "No error occurred");
    
    const HttpRequest& parsed = parser.request();
    TEST_ASSERT(parsed.method() == HttpMethod::POST, "Method parsed correctly");
    TEST_ASSERT(parsed.has_body(), "Request has body");
    TEST_ASSERT(parsed.body() == "{\"name\":\"test\"}", "Body parsed correctly");
}

void test_parser_incremental_parsing() {
    using namespace aevrix::http;
    
    HttpRequestParser parser;
    
    // Feed in chunks
    parser.feed("GET / HTTP/1.1\r\n");
    TEST_ASSERT(parser.state() == ParserState::Headers, "State progressed to Headers");
    
    parser.feed("Host: example.com\r\n");
    TEST_ASSERT(parser.state() == ParserState::Headers, "Still in Headers");
    
    parser.feed("\r\n");
    TEST_ASSERT(parser.is_complete(), "Parser completed");
}

void test_parser_invalid_method() {
    using namespace aevrix::http;
    
    HttpRequestParser parser;
    std::string request = "INVALID / HTTP/1.1\r\n\r\n";
    
    parser.feed(request);
    
    TEST_ASSERT(parser.has_error(), "Parser in error state");
    TEST_ASSERT(parser.error_message().find("Unknown HTTP method") != std::string::npos, 
                "Error message mentions unknown method");
}

void test_parser_missing_target() {
    using namespace aevrix::http;
    
    HttpRequestParser parser;
    std::string request = "GET HTTP/1.1\r\n\r\n";
    
    parser.feed(request);
    
    TEST_ASSERT(parser.has_error(), "Parser in error state");
    TEST_ASSERT(parser.error_message().find("Missing target") != std::string::npos, 
                "Error message mentions missing target");
}

void test_parser_invalid_header_format() {
    using namespace aevrix::http;
    
    HttpRequestParser parser;
    std::string request = "GET / HTTP/1.1\r\nInvalidHeader\r\n\r\n";
    
    parser.feed(request);
    
    TEST_ASSERT(parser.has_error(), "Parser in error state");
    TEST_ASSERT(parser.error_message().find("Invalid header format") != std::string::npos, 
                "Error message mentions invalid format");
}

void test_parser_header_case_insensitive() {
    using namespace aevrix::http;
    
    HttpRequestParser parser;
    std::string request = "GET / HTTP/1.1\r\n"
                         "Content-Type: text/plain\r\n"
                         "content-type: text/html\r\n"  // Should replace
                         "\r\n";
    
    parser.feed(request);
    
    TEST_ASSERT(parser.is_complete(), "Parser completed");
    const HttpRequest& parsed = parser.request();
    TEST_ASSERT(parsed.get_header("Content-Type") == "text/html", 
                "Case-insensitive header replacement works");
}

void test_parser_reset() {
    using namespace aevrix::http;
    
    HttpRequestParser parser;
    parser.feed("GET / HTTP/1.1\r\n\r\n");
    
    TEST_ASSERT(parser.is_complete(), "First request complete");
    
    parser.reset();
    TEST_ASSERT(parser.state() == ParserState::RequestLine, "State reset to RequestLine");
    TEST_ASSERT(!parser.is_complete(), "Parser no longer complete");
}

void test_parser_take_request() {
    using namespace aevrix::http;
    
    HttpRequestParser parser;
    parser.feed("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    
    HttpRequest request = parser.take_request();
    
    TEST_ASSERT(request.method() == HttpMethod::GET, "Taken request has correct method");
    TEST_ASSERT(parser.state() == ParserState::RequestLine, "Parser reset after take");
}

void test_parser_config_limits() {
    using namespace aevrix::http;
    
    ParserConfig config;
    config.max_request_line_bytes = 50;  // Very small limit
    
    HttpRequestParser parser(config);
    std::string long_request = "GET /very/long/path/that/exceeds/limit HTTP/1.1\r\n\r\n";
    
    parser.feed(long_request);
    
    TEST_ASSERT(parser.has_error(), "Parser in error state due to limit");
    TEST_ASSERT(parser.error_message().find("too long") != std::string::npos, 
                "Error message mentions limit exceeded");
}

void test_parser_head_request() {
    using namespace aevrix::http;
    
    HttpRequestParser parser;
    std::string request = "HEAD / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    
    parser.feed(request);
    
    TEST_ASSERT(parser.is_complete(), "HEAD request parsed successfully");
    const HttpRequest& parsed = parser.request();
    TEST_ASSERT(parsed.method() == HttpMethod::HEAD, "HEAD method parsed correctly");
}

void test_parser_multiple_headers() {
    using namespace aevrix::http;
    
    HttpRequestParser parser;
    std::string request = "GET / HTTP/1.1\r\n"
                         "Host: example.com\r\n"
                         "User-Agent: Aevrix/0.1.0\r\n"
                         "Accept: text/html\r\n"
                         "\r\n";
    
    parser.feed(request);
    
    TEST_ASSERT(parser.is_complete(), "Multiple headers parsed successfully");
    const HttpRequest& parsed = parser.request();
    TEST_ASSERT(parsed.headers().size() == 3, "Three headers parsed");
    TEST_ASSERT(parsed.get_header("Host") == "example.com", "First header correct");
    TEST_ASSERT(parsed.get_header("User-Agent") == "Aevrix/0.1.0", "Second header correct");
    TEST_ASSERT(parsed.get_header("Accept") == "text/html", "Third header correct");
}

// =============================================================================
// Integration Tests
// =============================================================================

void test_full_request_cycle() {
    using namespace aevrix::http;
    
    // Build a complex request
    HttpRequestParser parser;
    std::string request = "POST /api/users HTTP/1.1\r\n"
                         "Host: api.example.com\r\n"
                         "Content-Type: application/json\r\n"
                         "Content-Length: 27\r\n"
                         "User-Agent: Aevrix/0.1.0\r\n"
                         "Accept: application/json\r\n"
                         "\r\n"
                         "{\"username\":\"testuser\"}";
    
    parser.feed(request);
    
    TEST_ASSERT(parser.is_complete(), "Complex request parsed successfully");
    TEST_ASSERT(!parser.has_error(), "No errors during parsing");
    
    const HttpRequest& parsed = parser.request();
    TEST_ASSERT(parsed.method() == HttpMethod::POST, "Method correct");
    TEST_ASSERT(parsed.target() == "/api/users", "Target correct");
    TEST_ASSERT(parsed.version() == "HTTP/1.1", "Version correct");
    TEST_ASSERT(parsed.get_header("Host") == "api.example.com", "Host header correct");
    TEST_ASSERT(parsed.get_header("Content-Type") == "application/json", "Content-Type correct");
    TEST_ASSERT(parsed.get_header("Content-Length") == "27", "Content-Length correct");
    TEST_ASSERT(parsed.has_body(), "Body present");
    TEST_ASSERT(parsed.body() == "{\"username\":\"testuser\"}", "Body content correct");
}

void test_various_http_methods() {
    using namespace aevrix::http;
    
    // Test each supported method
    std::vector<HttpMethod> methods = {
        HttpMethod::GET, HttpMethod::HEAD, HttpMethod::POST, 
        HttpMethod::PUT, HttpMethod::HTTP_DELETE, HttpMethod::OPTIONS, HttpMethod::HTTP_PATCH
    };
    
    for (auto method : methods) {
        HttpRequestParser parser;
        std::string request = http_method_to_string(method) + " / HTTP/1.1\r\n\r\n";
        
        parser.feed(request);
        
        TEST_ASSERT(parser.is_complete(), http_method_to_string(method) + " request parsed");
        TEST_ASSERT(parser.request().method() == method, "Method matches");
        
        parser.reset();
    }
}

// =============================================================================
// Test Runner
// =============================================================================

int run_http_request_tests() {
    std::cout << "=== Running HTTP Request Unit Tests ===\n\n";
    
    try {
        // HttpMethod tests
        std::cout << "--- HttpMethod Tests ---\n";
        test_http_method_conversion();
        test_string_to_http_method();
        test_method_categorization();
        
        // HttpRequest tests
        std::cout << "\n--- HttpRequest Tests ---\n";
        test_request_default_construction();
        test_request_with_method_target();
        test_request_with_body();
        test_request_headers();
        test_request_validation();
        test_request_line();
        
        // HttpRequestParser tests
        std::cout << "\n--- HttpRequestParser Tests ---\n";
        test_parser_default_construction();
        test_parser_simple_get_request();
        test_parser_post_request_with_body();
        test_parser_incremental_parsing();
        test_parser_invalid_method();
        test_parser_missing_target();
        test_parser_invalid_header_format();
        test_parser_header_case_insensitive();
        test_parser_reset();
        test_parser_take_request();
        test_parser_config_limits();
        test_parser_head_request();
        test_parser_multiple_headers();
        
        // Integration tests
        std::cout << "\n--- Integration Tests ---\n";
        test_full_request_cycle();
        test_various_http_methods();
        
        std::cout << "\n=== All HTTP Request Tests PASSED ===\n";
        return 0;
    } catch (...) {
        std::cout << "\n=== HTTP Request Tests FAILED with exception ===\n";
        return 1;
    }
}

// =============================================================================
// Main Entry Point
// =============================================================================

int main() {
    return run_http_request_tests();
}
