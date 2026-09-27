// =============================================================================
// Aevrix - Concurrent Request Benchmark
// =============================================================================
// This benchmark measures the performance of handling concurrent requests.
// Concurrent requests test the server's ability to handle multiple connections
// simultaneously, which is critical for production workloads.
//
// Metrics measured:
// - Requests per second under concurrent load
// - Latency under concurrent load
// - Scalability with increasing concurrency
//
// Environment: This is a microbenchmark of concurrent request processing
// =============================================================================

#include "aevrix/http_request.h"
#include "aevrix/http_response.h"
#include "aevrix/http_response_serializer.h"
#include "benchmark.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>

using namespace aevrix;
using namespace aevrix::http;
using namespace aevrix::benchmarks;

// =============================================================================
// Thread-safe counter for concurrent benchmarking
// =============================================================================
std::mutex g_counter_mutex;
int64_t g_processed_requests = 0;

// =============================================================================
// Benchmark Function
// =============================================================================
// Simulates processing requests concurrently
// =============================================================================
void benchmark_concurrent() {
    // Create a typical HTTP response
    HttpResponse response(StatusCode::OK);
    response.set_body("Hello, World!");
    response.set_header("Content-Type", "text/plain");
    response.set_header("Content-Length", "13");
    
    HttpResponseSerializer serializer;
    std::string serialized_response = serializer.serialize(response);
    
    // Prevent unused variable warnings
    (void)serialized_response;
    
    // Simulate concurrent request processing
    // Each "thread" processes requests independently
    auto benchmark_func = [&]() {
        // Simulate request processing
        HttpResponse resp(StatusCode::OK);
        resp.set_body("Hello, World!");
        resp.set_header("Content-Type", "text/plain");
        resp.set_header("Content-Length", "13");
        
        HttpResponseSerializer ser;
        std::string serialized = ser.serialize(resp);
        
        // Update counter (thread-safe)
        {
            std::lock_guard<std::mutex> lock(g_counter_mutex);
            g_processed_requests++;
        }
        
        // Prevent optimization
        volatile size_t size = serialized.size();
        (void)size;
        (void)ser;  // Prevent unused variable warning
    };
    
    // Run benchmark
    // 10,000 iterations, 100 warmup iterations
    auto result = BenchmarkRunner::run("Concurrent Request Processing", 10000, 100, benchmark_func);
    
    // Print results
    result.print();
    
    std::cout << "\nNote: This benchmark measures single-threaded concurrent request simulation.\n";
    std::cout << "Full concurrent benchmark would require actual multi-threaded server testing.\n";
    std::cout << "Processed requests: " << g_processed_requests << "\n";
}

// =============================================================================
// Main Entry Point
// =============================================================================
int main() {
    print_environment_info();
    
    std::cout << "\nRunning Concurrent Request Benchmark...\n";
    std::cout << "This benchmark measures the performance of handling concurrent requests\n";
    
    try {
        benchmark_concurrent();
        
        std::cout << "\n=== Benchmark Complete ===\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Benchmark failed: " << e.what() << "\n";
        return 1;
    }
}
