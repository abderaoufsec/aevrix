// =============================================================================
// Aevrix - HTTP Response Unit Tests
// =============================================================================
// This file contains comprehensive unit tests for HTTP response components:
// - StatusCode enum and utilities
// - HttpHeader class
// - HttpHeaders collection
// - HttpResponse class
// - HttpResponseSerializer
//
// Tests cover:
// - Status code conversion and categorization
// - Header validation and case-insensitive handling
// - Response construction and manipulation
// - Serialization to wire format
// - Connection policy management
// - Content-Length handling
// =============================================================================

#include "aevrix/http_status.h"
#include "aevrix/http_header.h"
#include "aevrix/http_response.h"
#include "aevrix/http_response_serializer.h"
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
// StatusCode Tests
// =============================================================================

void test_status_code_conversion() {
    using namespace aevrix::http;
    
    TEST_ASSERT(status_code_to_int(StatusCode::OK) == 200, "OK converts to 200");
    TEST_ASSERT(status_code_to_int(StatusCode::NotFound) == 404, "NotFound converts to 404");
    TEST_ASSERT(status_code_to_int(StatusCode::InternalServerError) == 500, "InternalServerError converts to 500");
}

void test_status_code_to_string() {
    using namespace aevrix::http;
    
    TEST_ASSERT(status_code_to_string(StatusCode::OK) == "OK", "OK converts to 'OK'");
    TEST_ASSERT(status_code_to_string(StatusCode::NotFound) == "Not Found", "NotFound converts to 'Not Found'");
    TEST_ASSERT(status_code_to_string(StatusCode::BadRequest) == "Bad Request", "BadRequest converts to 'Bad Request'");
}

void test_status_code_categorization() {
    using namespace aevrix::http;
    
    TEST_ASSERT(is_success_status(StatusCode::OK), "OK is success status");
    TEST_ASSERT(is_success_status(StatusCode::Created), "Created is success status");
    TEST_ASSERT(!is_success_status(StatusCode::NotFound), "NotFound is not success status");
    
    TEST_ASSERT(is_client_error(StatusCode::BadRequest), "BadRequest is client error");
    TEST_ASSERT(is_client_error(StatusCode::NotFound), "NotFound is client error");
    TEST_ASSERT(!is_client_error(StatusCode::OK), "OK is not client error");
    
    TEST_ASSERT(is_server_error(StatusCode::InternalServerError), "InternalServerError is server error");
    TEST_ASSERT(is_server_error(StatusCode::ServiceUnavailable), "ServiceUnavailable is server error");
    TEST_ASSERT(!is_server_error(StatusCode::OK), "OK is not server error");
}

// =============================================================================
// HttpHeader Tests
// =============================================================================

void test_header_construction() {
    using namespace aevrix::http;
    
    HttpHeader header("Content-Type", "text/plain");
    TEST_ASSERT(header.name() == "Content-Type", "Header name preserved");
    TEST_ASSERT(header.value() == "text/plain", "Header value preserved");
    TEST_ASSERT(header.normalized_name() == "content-type", "Header name normalized");
}

void test_header_case_insensitive() {
    using namespace aevrix::http;
    
    HttpHeader header1("Content-Type", "text/plain");
    HttpHeader header2("content-type", "text/html");
    HttpHeader header3("CONTENT-TYPE", "application/json");
    
    TEST_ASSERT(header1.normalized_name() == header2.normalized_name(), 
                "Case-insensitive comparison works");
    TEST_ASSERT(header1.normalized_name() == header3.normalized_name(), 
                "Case-insensitive comparison works");
}

void test_header_validation() {
    using namespace aevrix::http;
    
    // Valid header
    HttpHeader valid_header("Content-Type", "text/plain");
    TEST_ASSERT(valid_header.is_valid_name(), "Valid header name");
    TEST_ASSERT(valid_header.is_valid_value(), "Valid header value");
    TEST_ASSERT(valid_header.is_valid(), "Valid header");
    
    // Invalid header name (contains space)
    HttpHeader invalid_name("Content Type", "text/plain");
    TEST_ASSERT(!invalid_name.is_valid_name(), "Invalid header name with space");
    
    // Invalid header value (contains null byte)
    HttpHeader invalid_value("Content-Type", std::string("text\0plain", 10));
    TEST_ASSERT(!invalid_value.is_valid_value(), "Invalid header value with null byte");
}

void test_header_whitespace_trimming() {
    using namespace aevrix::http;
    
    HttpHeader header("Content-Type", "  text/plain  ");
    TEST_ASSERT(header.value() == "text/plain", "Whitespace trimmed from value");
}

// =============================================================================
// HttpHeaders Tests
// =============================================================================

void test_headers_collection() {
    using namespace aevrix::http;
    
    HttpHeaders headers;
    TEST_ASSERT(headers.empty(), "Empty headers collection");
    TEST_ASSERT(headers.size() == 0, "Empty headers collection size");
    
    headers.set("Content-Type", "text/plain");
    TEST_ASSERT(!headers.empty(), "Headers collection not empty");
    TEST_ASSERT(headers.size() == 1, "Headers collection size after add");
}

void test_headers_get_set() {
    using namespace aevrix::http;
    
    HttpHeaders headers;
    headers.set("Content-Type", "text/plain");
    
    TEST_ASSERT(headers.get("Content-Type") == "text/plain", "Get header value");
    TEST_ASSERT(headers.get("content-type") == "text/plain", "Get header case-insensitive");
    TEST_ASSERT(headers.has("Content-Type"), "Header exists");
    TEST_ASSERT(headers.has("content-type"), "Header exists case-insensitive");
}

void test_headers_replace() {
    using namespace aevrix::http;
    
    HttpHeaders headers;
    headers.set("Content-Type", "text/plain");
    headers.set("Content-Type", "text/html");
    
    TEST_ASSERT(headers.get("Content-Type") == "text/html", "Header replaced");
    TEST_ASSERT(headers.size() == 1, "Size unchanged after replace");
}

void test_headers_remove() {
    using namespace aevrix::http;
    
    HttpHeaders headers;
    headers.set("Content-Type", "text/plain");
    headers.set("Content-Length", "100");
    
    TEST_ASSERT(headers.remove("Content-Type"), "Remove existing header");
    TEST_ASSERT(!headers.has("Content-Type"), "Header removed");
    TEST_ASSERT(headers.size() == 1, "Size decreased after remove");
    TEST_ASSERT(!headers.remove("Content-Type"), "Remove non-existent header");
}

// =============================================================================
// HttpResponse Tests
// =============================================================================

void test_response_default_construction() {
    using namespace aevrix::http;
    
    HttpResponse response;
    TEST_ASSERT(response.status() == StatusCode::OK, "Default status is OK");
    TEST_ASSERT(response.version() == "HTTP/1.1", "Default version is HTTP/1.1");
    TEST_ASSERT(!response.has_body(), "Default response has no body");
    TEST_ASSERT(response.headers().empty(), "Default response has no headers");
}

void test_response_with_status() {
    using namespace aevrix::http;
    
    HttpResponse response(StatusCode::NotFound);
    TEST_ASSERT(response.status() == StatusCode::NotFound, "Status set correctly");
}

void test_response_with_body() {
    using namespace aevrix::http;
    
    HttpResponse response(StatusCode::OK, "Hello, World!");
    TEST_ASSERT(response.has_body(), "Response has body");
    TEST_ASSERT(response.body() == "Hello, World!", "Body content correct");
    TEST_ASSERT(response.content_length() == 13, "Content length correct");
    TEST_ASSERT(response.get_header("Content-Length") == "13", "Content-Length header set");
}

void test_response_headers() {
    using namespace aevrix::http;
    
    HttpResponse response;
    response.set_header("Content-Type", "text/plain");
    response.set_header("Content-Length", "100");
    
    TEST_ASSERT(response.headers().size() == 2, "Headers count correct");
    TEST_ASSERT(response.get_header("Content-Type") == "text/plain", "Header value correct");
}

void test_response_connection_policy() {
    using namespace aevrix::http;
    
    HttpResponse response;
    response.set_connection_policy(ConnectionPolicy::KeepAlive);
    TEST_ASSERT(response.connection_policy() == ConnectionPolicy::KeepAlive, "Keep-alive policy set");
    TEST_ASSERT(response.get_header("Connection") == "keep-alive", "Connection header set");
    
    response.set_connection_policy(ConnectionPolicy::Close);
    TEST_ASSERT(response.connection_policy() == ConnectionPolicy::Close, "Close policy set");
    TEST_ASSERT(response.get_header("Connection") == "close", "Connection header updated");
}

void test_response_validation() {
    using namespace aevrix::http;
    
    HttpResponse response(StatusCode::OK, "Hello");
    TEST_ASSERT(response.is_valid(), "Valid response");
    
    // Response with mismatched Content-Length
    response.set_header("Content-Length", "999");
    TEST_ASSERT(!response.is_valid(), "Invalid response with mismatched Content-Length");
}

void test_response_reset() {
    using namespace aevrix::http;
    
    HttpResponse response(StatusCode::NotFound, "Error");
    response.set_header("Content-Type", "text/plain");
    
    response.reset();
    
    TEST_ASSERT(response.status() == StatusCode::OK, "Status reset to OK");
    TEST_ASSERT(!response.has_body(), "Body cleared");
    TEST_ASSERT(response.headers().empty(), "Headers cleared");
}

// =============================================================================
// HttpResponseSerializer Tests
// =============================================================================

void test_serializer_basic_response() {
    using namespace aevrix::http;
    
    HttpResponse response(StatusCode::OK, "Hello, World!");
    response.set_header("Content-Type", "text/plain");
    
    std::string serialized = HttpResponseSerializer::serialize(response);
    
    TEST_ASSERT(!serialized.empty(), "Serialization produces output");
    TEST_ASSERT(serialized.find("HTTP/1.1 200 OK") == 0, "Status line correct");
    TEST_ASSERT(serialized.find("Content-Type: text/plain") != std::string::npos, "Header present");
    TEST_ASSERT(serialized.find("Hello, World!") != std::string::npos, "Body present");
}

void test_serializer_status_line() {
    using namespace aevrix::http;
    
    HttpResponse response(StatusCode::NotFound);
    std::string status_line = HttpResponseSerializer::serialize_status_line(response);
    
    TEST_ASSERT(status_line == "HTTP/1.1 404 Not Found\r\n", "Status line format correct");
}

void test_serializer_headers() {
    using namespace aevrix::http;
    
    HttpResponse response;
    response.set_header("Content-Type", "text/plain");
    response.set_header("Content-Length", "13");
    
    std::string headers = HttpResponseSerializer::serialize_headers(response);
    
    TEST_ASSERT(headers.find("Content-Type: text/plain") != std::string::npos, "Header present");
    TEST_ASSERT(headers.find("Content-Length: 13") != std::string::npos, "Header present");
    TEST_ASSERT(headers.find("\r\n\r\n") != std::string::npos, "Empty line after headers");
}

void test_serializer_crlf_endings() {
    using namespace aevrix::http;
    
    HttpResponse response(StatusCode::OK, "Test");
    std::string serialized = HttpResponseSerializer::serialize(response);
    
    // Check for CRLF line endings
    size_t crlf_count = 0;
    size_t pos = 0;
    while ((pos = serialized.find("\r\n", pos)) != std::string::npos) {
        crlf_count++;
        pos += 2;
    }
    
    TEST_ASSERT(crlf_count >= 2, "CRLF line endings used");
}

void test_serializer_validation() {
    using namespace aevrix::http;
    
    HttpResponse response(StatusCode::OK, "Test");
    std::string serialized = HttpResponseSerializer::serialize(response);
    
    TEST_ASSERT(HttpResponseSerializer::validate_serialized(serialized), "Valid serialized response");
    TEST_ASSERT(!HttpResponseSerializer::validate_serialized(""), "Empty string invalid");
    TEST_ASSERT(!HttpResponseSerializer::validate_serialized("Invalid"), "Invalid format");
}

void test_serializer_invalid_response() {
    using namespace aevrix::http;
    
    // Create a response with invalid Content-Length
    HttpResponse response(StatusCode::OK, "Hello");
    response.set_header("Content-Length", "invalid");
    
    std::string serialized = HttpResponseSerializer::serialize(response);
    TEST_ASSERT(serialized.empty(), "Invalid response produces empty serialization");
}

// =============================================================================
// Integration Tests
// =============================================================================

void test_full_response_cycle() {
    using namespace aevrix::http;
    
    // Build a complete response
    HttpResponse response(StatusCode::OK, "Hello from Aevrix!");
    response.set_header("Content-Type", "text/plain");
    response.set_header("Server", "Aevrix/0.1.0");
    response.set_connection_policy(ConnectionPolicy::KeepAlive);
    
    // Serialize it
    std::string serialized = HttpResponseSerializer::serialize(response);
    
    // Verify the serialized format
    TEST_ASSERT(serialized.find("HTTP/1.1 200 OK") == 0, "Status line correct");
    TEST_ASSERT(serialized.find("Content-Type: text/plain") != std::string::npos, "Content-Type header");
    TEST_ASSERT(serialized.find("Content-Length: 18") != std::string::npos, "Content-Length header");
    TEST_ASSERT(serialized.find("Server: Aevrix/0.1.0") != std::string::npos, "Server header");
    TEST_ASSERT(serialized.find("Connection: keep-alive") != std::string::npos, "Connection header");
    TEST_ASSERT(serialized.find("Hello from Aevrix!") != std::string::npos, "Body content");
    TEST_ASSERT(serialized.find("\r\n\r\n") != std::string::npos, "Empty line separator");
}

void test_error_responses() {
    using namespace aevrix::http;
    
    // Test 404 Not Found
    HttpResponse not_found(StatusCode::NotFound, "Resource not found");
    not_found.set_header("Content-Type", "text/plain");
    std::string not_found_serialized = HttpResponseSerializer::serialize(not_found);
    TEST_ASSERT(not_found_serialized.find("404 Not Found") != std::string::npos, "404 status");
    
    // Test 500 Internal Server Error
    HttpResponse server_error(StatusCode::InternalServerError, "Internal error");
    server_error.set_header("Content-Type", "text/plain");
    std::string server_error_serialized = HttpResponseSerializer::serialize(server_error);
    TEST_ASSERT(server_error_serialized.find("500 Internal Server Error") != std::string::npos, "500 status");
}

// =============================================================================
// Test Runner
// =============================================================================

int run_http_response_tests() {
    std::cout << "=== Running HTTP Response Unit Tests ===\n\n";
    
    try {
        // StatusCode tests
        std::cout << "--- StatusCode Tests ---\n";
        test_status_code_conversion();
        test_status_code_to_string();
        test_status_code_categorization();
        
        // HttpHeader tests
        std::cout << "\n--- HttpHeader Tests ---\n";
        test_header_construction();
        test_header_case_insensitive();
        test_header_validation();
        test_header_whitespace_trimming();
        
        // HttpHeaders tests
        std::cout << "\n--- HttpHeaders Tests ---\n";
        test_headers_collection();
        test_headers_get_set();
        test_headers_replace();
        test_headers_remove();
        
        // HttpResponse tests
        std::cout << "\n--- HttpResponse Tests ---\n";
        test_response_default_construction();
        test_response_with_status();
        test_response_with_body();
        test_response_headers();
        test_response_connection_policy();
        test_response_validation();
        test_response_reset();
        
        // HttpResponseSerializer tests
        std::cout << "\n--- HttpResponseSerializer Tests ---\n";
        test_serializer_basic_response();
        test_serializer_status_line();
        test_serializer_headers();
        test_serializer_crlf_endings();
        test_serializer_validation();
        test_serializer_invalid_response();
        
        // Integration tests
        std::cout << "\n--- Integration Tests ---\n";
        test_full_response_cycle();
        test_error_responses();
        
        std::cout << "\n=== All HTTP Response Tests PASSED ===\n";
        return 0;
    } catch (...) {
        std::cout << "\n=== HTTP Response Tests FAILED with exception ===\n";
        return 1;
    }
}

// =============================================================================
// Main Entry Point
// =============================================================================

int main() {
    return run_http_response_tests();
}
