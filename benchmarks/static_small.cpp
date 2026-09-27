// =============================================================================
// Aevrix - Static Small File Benchmark
// =============================================================================
// This benchmark measures the performance of serving small static files.
// Small files are typically HTML, CSS, or JS files under 10KB.
//
// Metrics measured:
// - Requests per second
// - Latency (mean, p50, p95, p99)
// - Throughput
//
// Environment: This is a microbenchmark of the static file serving logic
// =============================================================================

#include "aevrix/static_file_server.h"
#include "aevrix/http_response_serializer.h"
#include "benchmark.h"
#include <iostream>
#include <fstream>
#include <filesystem>

using namespace aevrix;
using namespace aevrix::benchmarks;

// =============================================================================
// Benchmark Function
// =============================================================================
// Simulates serving a small static file multiple times
// =============================================================================
void benchmark_static_small() {
    // Create a temporary small file for benchmarking
    const std::string test_file = "public/small_test.html";
    const std::string test_content = "<!DOCTYPE html><html><head><title>Test</title></head>"
                                    "<body><h1>Small Test File</h1></body></html>";
    
    // Ensure public directory exists
    try {
        std::filesystem::create_directories("public");
    } catch (const std::exception& e) {
        std::cerr << "Failed to create public directory: " << e.what() << "\n";
        throw;
    }
    
    // Write test file
    std::ofstream file(test_file);
    if (!file) {
        std::cerr << "Failed to create test file: " << test_file << "\n";
        throw std::runtime_error("Failed to create test file");
    }
    file << test_content;
    file.close();
    
    // Create static file server
    try {
        StaticFileServer server("./public");
        
        // Benchmark the file serving operation
        auto benchmark_func = [&]() {
            auto [content, mime_type, status] = server.serve_file("/small_test.html");
            // Simulate serialization
            http::HttpResponse response(http::StatusCode::OK);
            response.set_body(content);
            response.set_header("Content-Type", mime_type);
            http::HttpResponseSerializer serializer;
            std::string serialized = serializer.serialize(response);
            // Prevent optimization
            volatile size_t size = serialized.size();
            (void)size;
        };
        
        // Run benchmark
        // 10,000 iterations, 100 warmup iterations
        auto result = BenchmarkRunner::run("Static Small File Serving", 10000, 100, benchmark_func);
        
        // Print results
        result.print();
        
        // Cleanup
        std::filesystem::remove(test_file);
    } catch (const std::exception& e) {
        std::cerr << "StaticFileServer creation failed: " << e.what() << "\n";
        // Cleanup
        std::filesystem::remove(test_file);
        throw;
    }
}

// =============================================================================
// Main Entry Point
// =============================================================================
int main() {
    print_environment_info();
    
    std::cout << "\nRunning Static Small File Benchmark...\n";
    std::cout << "This benchmark measures the performance of serving small static files (<10KB)\n";
    
    try {
        benchmark_static_small();
        
        std::cout << "\n=== Benchmark Complete ===\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Benchmark failed: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Benchmark failed with unknown exception\n";
        return 1;
    }
}
