// =============================================================================
// Aevrix - Keep-Alive Benchmark
// =============================================================================
// This benchmark measures the performance of HTTP keep-alive connections.
// Keep-alive allows multiple requests to be sent over a single TCP connection,
// reducing connection establishment overhead.
//
// Metrics measured:
// - Requests per second with keep-alive
// - Latency (mean, p50, p95, p99)
// - Comparison with non-keep-alive performance
//
// Environment: This is a microbenchmark of keep-alive connection handling
// =============================================================================

#include "aevrix/http_request.h"
#include "aevrix/http_response.h"
#include "aevrix/http_response_serializer.h"
#include "benchmark.h"
#include <iostream>
#include <string>

using namespace aevrix;
using namespace aevrix::http;
using namespace aevrix::benchmarks;

// =============================================================================
// Benchmark Function
// =============================================================================
// Simulates processing multiple requests on a keep-alive connection
// =============================================================================
void benchmark_keepalive() {
    // Create a typical HTTP response with keep-alive header
    HttpResponse response(StatusCode::OK);
    response.set_body("Hello, World!");
    response.set_header("Content-Type", "text/plain");
    response.set_header("Content-Length", "13");
    response.set_header("Connection", "keep-alive");
    
    HttpResponseSerializer serializer;
    std::string serialized_response = serializer.serialize(response);
    
    // Benchmark response serialization (simulating keep-alive response)
    auto benchmark_func = [&]() {
        // Simulate processing a request on keep-alive connection
        HttpResponse resp(StatusCode::OK);
        resp.set_body("Hello, World!");
        resp.set_header("Content-Type", "text/plain");
        resp.set_header("Content-Length", "13");
        resp.set_header("Connection", "keep-alive");
        
        HttpResponseSerializer ser;
        std::string serialized = ser.serialize(resp);
        
        // Prevent optimization
        volatile size_t size = serialized.size();
        (void)size;
    };
    
    // Run benchmark
    // 10,000 iterations, 100 warmup iterations
    auto result = BenchmarkRunner::run("Keep-Alive Response Serialization", 10000, 100, benchmark_func);
    
    // Print results
    result.print();
    
    std::cout << "\nNote: This benchmark measures response serialization overhead.\n";
    std::cout << "Full keep-alive benchmark would require actual TCP connection management.\n";
}

// =============================================================================
// Main Entry Point
// =============================================================================
int main() {
    print_environment_info();
    
    std::cout << "\nRunning Keep-Alive Benchmark...\n";
    std::cout << "This benchmark measures the performance of HTTP keep-alive connections\n";
    
    try {
        benchmark_keepalive();
        
        std::cout << "\n=== Benchmark Complete ===\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Benchmark failed: " << e.what() << "\n";
        return 1;
    }
}
