// =============================================================================
// Aevrix - Static Large File Benchmark
// =============================================================================
// This benchmark measures the performance of serving large static files.
// Large files are typically images, videos, or assets over 1MB.
//
// Metrics measured:
// - Requests per second
// - Latency (mean, p50, p95, p99)
// - Throughput (MB/s)
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
// Simulates serving a large static file multiple times
// =============================================================================
void benchmark_static_large() {
    // Create a temporary large file for benchmarking (1MB)
    const std::string test_file = "public/large_test.bin";
    const size_t file_size = 1024 * 1024;  // 1MB
    
    // Ensure public directory exists
    try {
        std::filesystem::create_directories("public");
    } catch (const std::exception& e) {
        std::cerr << "Failed to create public directory: " << e.what() << "\n";
        throw;
    }
    
    // Write test file with dummy data
    std::ofstream file(test_file, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to create test file: " << test_file << "\n";
        throw std::runtime_error("Failed to create test file");
    }
    std::vector<char> buffer(file_size, 'X');
    file.write(buffer.data(), buffer.size());
    file.close();
    
    // Create static file server
    try {
        StaticFileServer server("./public");
        
        // Benchmark the file serving operation
        auto benchmark_func = [&]() {
            auto [content, mime_type, status] = server.serve_file("/large_test.bin");
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
        
        // Run benchmark with fewer iterations for large files
        // 1,000 iterations, 10 warmup iterations
        auto result = BenchmarkRunner::run("Static Large File Serving", 1000, 10, benchmark_func);
        
        // Print results
        result.print();
        
        // Calculate throughput in MB/s
        double throughput_mbps = (file_size * result.iterations) / (result.total_time_s * 1024 * 1024);
        std::cout << "Throughput: " << std::fixed << std::setprecision(2) << throughput_mbps << " MB/s\n";
        
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
    
    std::cout << "\nRunning Static Large File Benchmark...\n";
    std::cout << "This benchmark measures the performance of serving large static files (>1MB)\n";
    
    try {
        benchmark_static_large();
        
        std::cout << "\n=== Benchmark Complete ===\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Benchmark failed: " << e.what() << "\n";
        return 1;
    }
}
