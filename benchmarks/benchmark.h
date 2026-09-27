// =============================================================================
// Aevrix - Benchmark Utilities
// =============================================================================
// This header provides utilities for performance benchmarking.
// Includes high-resolution timing utilities and result formatting.
//
// Usage:
//   BenchmarkTimer timer;
//   timer.start();
//   // ... code to benchmark ...
//   timer.stop();
//   std::cout << "Elapsed: " << timer.elapsed_ms() << " ms\n";
// =============================================================================

#pragma once

#include <chrono>
#include <iostream>
#include <string>
#include <iomanip>
#include <vector>
#include <numeric>
#include <algorithm>
#include <functional>

namespace aevrix {
namespace benchmarks {

// =============================================================================
// BenchmarkTimer Class
// =============================================================================
// High-resolution timer for benchmarking code execution time.
// Uses std::chrono::high_resolution_clock for nanosecond precision.
// =============================================================================
class BenchmarkTimer {
public:
    /**
     * @brief Constructor - initializes timer in stopped state
     */
    BenchmarkTimer() : running_(false) {}

    /**
     * @brief Start the timer
     */
    void start() {
        start_time_ = std::chrono::high_resolution_clock::now();
        running_ = true;
    }

    /**
     * @brief Stop the timer
     */
    void stop() {
        end_time_ = std::chrono::high_resolution_clock::now();
        running_ = false;
    }

    /**
     * @brief Get elapsed time in nanoseconds
     * @return int64_t Elapsed time in nanoseconds
     */
    int64_t elapsed_ns() const {
        auto end = running_ ? std::chrono::high_resolution_clock::now() : end_time_;
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_time_).count();
    }

    /**
     * @brief Get elapsed time in microseconds
     * @return int64_t Elapsed time in microseconds
     */
    int64_t elapsed_us() const {
        return elapsed_ns() / 1000;
    }

    /**
     * @brief Get elapsed time in milliseconds
     * @return int64_t Elapsed time in milliseconds
     */
    int64_t elapsed_ms() const {
        return elapsed_ns() / 1000000;
    }

    /**
     * @brief Get elapsed time in seconds
     * @return double Elapsed time in seconds
     */
    double elapsed_s() const {
        return static_cast<double>(elapsed_ns()) / 1000000000.0;
    }

    /**
     * @brief Check if timer is currently running
     * @return true if running, false otherwise
     */
    bool is_running() const {
        return running_;
    }

private:
    std::chrono::high_resolution_clock::time_point start_time_;
    std::chrono::high_resolution_clock::time_point end_time_;
    bool running_;
};

// =============================================================================
// BenchmarkResult Struct
// =============================================================================
// Stores benchmark results including timing statistics
// =============================================================================
struct BenchmarkResult {
    std::string name;           // Benchmark name
    int64_t iterations;         // Number of iterations
    double total_time_s;        // Total time in seconds
    double mean_time_us;        // Mean time per iteration (microseconds)
    double min_time_us;         // Minimum time per iteration (microseconds)
    double max_time_us;         // Maximum time per iteration (microseconds)
    double p50_time_us;         // 50th percentile (microseconds)
    double p95_time_us;         // 95th percentile (microseconds)
    double p99_time_us;         // 99th percentile (microseconds)
    double ops_per_sec;         // Operations per second

    /**
     * @brief Print benchmark results to stdout
     */
    void print() const {
        std::cout << "\n=== " << name << " ===\n";
        std::cout << "Iterations: " << iterations << "\n";
        std::cout << "Total time: " << std::fixed << std::setprecision(3) << total_time_s << " s\n";
        std::cout << "Ops/sec: " << std::fixed << std::setprecision(0) << ops_per_sec << "\n";
        std::cout << "\nLatency (microseconds):\n";
        std::cout << "  Mean: " << std::fixed << std::setprecision(2) << mean_time_us << "\n";
        std::cout << "  Min:  " << std::fixed << std::setprecision(2) << min_time_us << "\n";
        std::cout << "  Max:  " << std::fixed << std::setprecision(2) << max_time_us << "\n";
        std::cout << "  P50:  " << std::fixed << std::setprecision(2) << p50_time_us << "\n";
        std::cout << "  P95:  " << std::fixed << std::setprecision(2) << p95_time_us << "\n";
        std::cout << "  P99:  " << std::fixed << std::setprecision(2) << p99_time_us << "\n";
    }
};

// =============================================================================
// BenchmarkRunner Class
// =============================================================================
// Runs benchmarks multiple times and calculates statistics
// =============================================================================
class BenchmarkRunner {
public:
    /**
     * @brief Run a benchmark function multiple times
     * 
     * @param name Benchmark name
     * @param iterations Number of iterations to run
     * @param warmup_iterations Number of warmup iterations (not counted)
     * @param func Function to benchmark (returns void, takes no arguments)
     * @return BenchmarkResult Statistics from the benchmark run
     */
    static BenchmarkResult run(const std::string& name, int64_t iterations, 
                                int64_t warmup_iterations, 
                                std::function<void()> func) {
        BenchmarkResult result;
        result.name = name;
        result.iterations = iterations;
        
        std::vector<double> times_us;
        times_us.reserve(iterations);
        
        // Warmup runs (not measured)
        for (int64_t i = 0; i < warmup_iterations; ++i) {
            func();
        }
        
        // Timed runs
        BenchmarkTimer timer;
        for (int64_t i = 0; i < iterations; ++i) {
            timer.start();
            func();
            timer.stop();
            times_us.push_back(static_cast<double>(timer.elapsed_us()));
        }
        
        // Calculate statistics
        result.total_time_s = std::accumulate(times_us.begin(), times_us.end(), 0.0) / 1000000.0;
        result.mean_time_us = result.total_time_s * 1000000.0 / static_cast<double>(iterations);
        result.min_time_us = *std::min_element(times_us.begin(), times_us.end());
        result.max_time_us = *std::max_element(times_us.begin(), times_us.end());
        
        // Calculate percentiles
        std::sort(times_us.begin(), times_us.end());
        result.p50_time_us = times_us[static_cast<size_t>(static_cast<double>(iterations) * 0.50)];
        result.p95_time_us = times_us[static_cast<size_t>(static_cast<double>(iterations) * 0.95)];
        result.p99_time_us = times_us[static_cast<size_t>(static_cast<double>(iterations) * 0.99)];
        
        // Calculate ops/sec
        result.ops_per_sec = static_cast<double>(iterations) / result.total_time_s;
        
        return result;
    }
};

// =============================================================================
// Environment Information
// =============================================================================
// Prints information about the benchmark environment
// =============================================================================
void print_environment_info() {
    std::cout << "\n=== Benchmark Environment ===\n";
    std::cout << "Platform: ";
#ifdef _WIN32
    std::cout << "Windows\n";
#else
    std::cout << "Unix/Linux\n";
#endif
    
    std::cout << "Compiler: ";
#ifdef __GNUC__
    std::cout << "GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << "\n";
#elif defined(__clang__)
    std::cout << "Clang " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__ << "\n";
#elif defined(_MSC_VER)
    std::cout << "MSVC " << _MSC_VER << "\n";
#else
    std::cout << "Unknown\n";
#endif
    
    std::cout << "C++ Standard: C++" << __cplusplus << "\n";
    std::cout << "Build Type: ";
#ifdef NDEBUG
    std::cout << "Release\n";
#else
    std::cout << "Debug\n";
#endif
    std::cout << "===========================\n";
}

} // namespace benchmarks
} // namespace aevrix
