// =============================================================================
// Aevrix - Metrics Tests
// =============================================================================
// Unit tests for the metrics collection system.
// =============================================================================

#include "aevrix/metrics.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

using namespace aevrix;

// =============================================================================
// Test Helper Functions
// =============================================================================

void test_request_metrics() {
    std::cout << "Testing request metrics..." << std::endl;
    
    Metrics metrics;
    
    // Test incrementing total requests
    metrics.increment_requests();
    metrics.increment_requests();
    metrics.increment_requests();
    
    assert(metrics.total_requests() == 3);
    std::cout << "  increment_requests() works" << std::endl;
    
    // Test incrementing status codes
    metrics.increment_status_code(200);
    metrics.increment_status_code(200);
    metrics.increment_status_code(404);
    
    assert(metrics.status_code_count(200) == 2);
    assert(metrics.status_code_count(404) == 1);
    std::cout << "  increment_status_code() works" << std::endl;
    
    // Test non-existent status code
    assert(metrics.status_code_count(500) == 0);
    std::cout << "  status_code_count() returns 0 for non-existent code" << std::endl;
}

void test_connection_metrics() {
    std::cout << "Testing connection metrics..." << std::endl;
    
    Metrics metrics;
    
    // Test incrementing total connections
    metrics.increment_connections();
    metrics.increment_connections();
    
    assert(metrics.total_connections() == 2);
    std::cout << "  increment_connections() works" << std::endl;
    
    // Test active connections
    metrics.increment_active_connections();
    metrics.increment_active_connections();
    assert(metrics.active_connections() == 2);
    
    metrics.decrement_active_connections();
    assert(metrics.active_connections() == 1);
    std::cout << "  active_connections increment/decrement works" << std::endl;
}

void test_error_metrics() {
    std::cout << "Testing error metrics..." << std::endl;
    
    Metrics metrics;
    
    // Test incrementing total errors
    metrics.increment_errors();
    metrics.increment_errors();
    
    assert(metrics.total_errors() == 2);
    std::cout << "  increment_errors() works" << std::endl;
    
    // Test incrementing error types
    metrics.increment_error_type("parse_error");
    metrics.increment_error_type("parse_error");
    metrics.increment_error_type("timeout");
    
    assert(metrics.error_type_count("parse_error") == 2);
    assert(metrics.error_type_count("timeout") == 1);
    std::cout << "  increment_error_type() works" << std::endl;
    
    // Test non-existent error type
    assert(metrics.error_type_count("io_error") == 0);
    std::cout << "  error_type_count() returns 0 for non-existent type" << std::endl;
}

void test_latency_metrics() {
    std::cout << "Testing latency metrics..." << std::endl;
    
    Metrics metrics;
    
    // Record some latency samples
    for (int i = 0; i < 100; i++) {
        metrics.record_latency(100 + i);  // 100-199 microseconds
    }
    
    // Test mean latency
    uint64_t mean = metrics.latency_mean();
    assert(mean >= 100 && mean <= 200);
    std::cout << "  latency_mean() works: " << mean << " us" << std::endl;
    
    // Test percentiles
    uint64_t p50 = metrics.latency_p50();
    uint64_t p95 = metrics.latency_p95();
    uint64_t p99 = metrics.latency_p99();
    
    assert(p50 >= 100 && p50 <= 200);
    assert(p95 >= 100 && p95 <= 200);
    assert(p99 >= 100 && p99 <= 200);
    
    std::cout << "  latency_p50() works: " << p50 << " us" << std::endl;
    std::cout << "  latency_p95() works: " << p95 << " us" << std::endl;
    std::cout << "  latency_p99() works: " << p99 << " us" << std::endl;
    
    // Test empty latency
    Metrics empty_metrics;
    assert(empty_metrics.latency_mean() == 0);
    assert(empty_metrics.latency_p50() == 0);
    std::cout << "  latency functions return 0 when no samples" << std::endl;
}

void test_byte_metrics() {
    std::cout << "Testing byte metrics..." << std::endl;
    
    Metrics metrics;
    
    // Test bytes sent
    metrics.add_bytes_sent(1024);
    metrics.add_bytes_sent(2048);
    
    assert(metrics.bytes_sent() == 3072);
    std::cout << "  add_bytes_sent() works" << std::endl;
    
    // Test bytes received
    metrics.add_bytes_received(512);
    metrics.add_bytes_received(1024);
    
    assert(metrics.bytes_received() == 1536);
    std::cout << "  add_bytes_received() works" << std::endl;
}

void test_parser_failure_metrics() {
    std::cout << "Testing parser failure metrics..." << std::endl;
    
    Metrics metrics;
    
    metrics.increment_parser_failures();
    metrics.increment_parser_failures();
    metrics.increment_parser_failures();
    
    assert(metrics.parser_failures() == 3);
    std::cout << "  increment_parser_failures() works" << std::endl;
}

void test_metrics_export_prometheus() {
    std::cout << "Testing Prometheus export..." << std::endl;
    
    Metrics metrics;
    metrics.increment_requests();
    metrics.increment_status_code(200);
    metrics.increment_connections();
    metrics.increment_active_connections();
    
    std::string prometheus = metrics.export_prometheus();
    
    assert(!prometheus.empty());
    assert(prometheus.find("aevrix_requests_total") != std::string::npos);
    assert(prometheus.find("aevrix_connections_total") != std::string::npos);
    assert(prometheus.find("aevrix_connections_active") != std::string::npos);
    
    std::cout << "  export_prometheus() works" << std::endl;
}

void test_metrics_export_text() {
    std::cout << "Testing text export..." << std::endl;
    
    Metrics metrics;
    metrics.increment_requests();
    metrics.increment_connections();
    
    std::string text = metrics.export_text();
    
    assert(!text.empty());
    assert(text.find("Aevrix Server Metrics") != std::string::npos);
    assert(text.find("Requests:") != std::string::npos);
    assert(text.find("Connections:") != std::string::npos);
    
    std::cout << "  export_text() works" << std::endl;
}

void test_metrics_reset() {
    std::cout << "Testing metrics reset..." << std::endl;
    
    Metrics metrics;
    metrics.increment_requests();
    metrics.increment_connections();
    metrics.increment_errors();
    metrics.add_bytes_sent(1024);
    
    assert(metrics.total_requests() > 0);
    assert(metrics.total_connections() > 0);
    assert(metrics.total_errors() > 0);
    assert(metrics.bytes_sent() > 0);
    
    metrics.reset();
    
    assert(metrics.total_requests() == 0);
    assert(metrics.total_connections() == 0);
    assert(metrics.total_errors() == 0);
    assert(metrics.bytes_sent() == 0);
    
    std::cout << "  reset() works" << std::endl;
}

void test_thread_safety() {
    std::cout << "Testing thread safety..." << std::endl;
    
    Metrics metrics;
    const int num_threads = 10;
    const int iterations = 1000;
    
    std::vector<std::thread> threads;
    
    // Spawn multiple threads incrementing metrics
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([&metrics, iterations]() {
            for (int j = 0; j < iterations; j++) {
                metrics.increment_requests();
                metrics.increment_connections();
                metrics.increment_errors();
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify final counts
    assert(metrics.total_requests() == num_threads * iterations);
    assert(metrics.total_connections() == num_threads * iterations);
    assert(metrics.total_errors() == num_threads * iterations);
    
    std::cout << "  Thread safety works (" << num_threads * iterations << " increments)" << std::endl;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    std::cout << "=== Metrics Tests ===" << std::endl;
    std::cout << std::endl;
    
    test_request_metrics();
    std::cout << std::endl;
    
    test_connection_metrics();
    std::cout << std::endl;
    
    test_error_metrics();
    std::cout << std::endl;
    
    test_latency_metrics();
    std::cout << std::endl;
    
    test_byte_metrics();
    std::cout << std::endl;
    
    test_parser_failure_metrics();
    std::cout << std::endl;
    
    test_metrics_export_prometheus();
    std::cout << std::endl;
    
    test_metrics_export_text();
    std::cout << std::endl;
    
    test_metrics_reset();
    std::cout << std::endl;
    
    test_thread_safety();
    std::cout << std::endl;
    
    std::cout << "=== All Metrics Tests Passed ===" << std::endl;
    return 0;
}
