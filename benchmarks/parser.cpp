// =============================================================================
// Aevrix - HTTP Parser Benchmark
// =============================================================================
// This benchmark measures the performance of HTTP request parsing.
// HTTP parsing is a critical path in request processing, so its performance
// directly impacts overall server throughput.
//
// Metrics measured:
// - Requests parsed per second
// - Parse latency (mean, p50, p95, p99)
// - Parser throughput
//
// Environment: This is a microbenchmark of the HTTP request parser
// =============================================================================

#include "aevrix/http_request_parser.h"
#include "benchmark.h"
#include <iostream>
#include <string>

using namespace aevrix;
using namespace aevrix::http;
using namespace aevrix::benchmarks;

// =============================================================================
// Benchmark Function
// =============================================================================
// Simulates parsing HTTP requests
// =============================================================================
void benchmark_parser() {
    // Create a typical HTTP GET request
    std::string get_request = "GET /index.html HTTP/1.1\r\n"
                             "Host: example.com\r\n"
                             "User-Agent: Aevrix/0.1.0\r\n"
                             "Accept: text/html\r\n"
                             "\r\n";
    
    // Create a typical HTTP POST request with body
    std::string post_request = "POST /api/users HTTP/1.1\r\n"
                              "Host: example.com\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: 23\r\n"
                              "\r\n"
                              "{\"username\":\"testuser\"}";
    
    // Benchmark GET request parsing
    auto benchmark_get = [&]() {
        HttpRequestParser parser;
        parser.feed(get_request);
        // Prevent optimization
        volatile bool complete = parser.is_complete();
        (void)complete;
    };
    
    auto get_result = BenchmarkRunner::run("HTTP GET Request Parsing", 10000, 100, benchmark_get);
    get_result.print();
    
    // Benchmark POST request parsing
    auto benchmark_post = [&]() {
        HttpRequestParser parser;
        parser.feed(post_request);
        // Prevent optimization
        volatile bool complete = parser.is_complete();
        (void)complete;
    };
    
    auto post_result = BenchmarkRunner::run("HTTP POST Request Parsing", 10000, 100, benchmark_post);
    post_result.print();
    
    // Calculate combined statistics
    double total_ops = get_result.ops_per_sec + post_result.ops_per_sec;
    std::cout << "\nCombined Parser Throughput: " << std::fixed << std::setprecision(0) 
              << total_ops << " req/sec\n";
}

// =============================================================================
// Main Entry Point
// =============================================================================
int main() {
    print_environment_info();
    
    std::cout << "\nRunning HTTP Parser Benchmark...\n";
    std::cout << "This benchmark measures the performance of HTTP request parsing\n";
    
    try {
        benchmark_parser();
        
        std::cout << "\n=== Benchmark Complete ===\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Benchmark failed: " << e.what() << "\n";
        return 1;
    }
}
