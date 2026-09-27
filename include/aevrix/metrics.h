// =============================================================================
// Aevrix - Server Metrics
// =============================================================================
// This header provides a metrics collection system for tracking server behavior.
// Metrics enable observability without attaching a debugger, allowing developers
// to understand server performance and health at runtime.
//
// Metrics Tracked:
// - Requests: Total requests, requests per status code
// - Connections: Total connections, active connections
// - Errors: Total errors, errors by type
// - Latency: Request latency percentiles (p50, p95, p99)
// - Bytes: Bytes sent, bytes received
// - Parser Failures: Parse errors, malformed requests
//
// Phase 20 Implementation:
// - Metrics collection system
// - Thread-safe atomic counters
// - Histogram for latency tracking
// - Metrics output in Prometheus-compatible format
// =============================================================================

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

namespace aevrix {

// =============================================================================
// Metrics Class
// =============================================================================
// Thread-safe metrics collection for server observability.
// All metrics are atomic to ensure thread-safe updates from multiple threads.
// =============================================================================
class Metrics {
public:
    // =========================================================================
    // Constructor
    // =========================================================================

    /**
     * @brief Constructor - initializes all metrics to zero
     */
    Metrics();

    // =========================================================================
    // Request Metrics
    // =========================================================================

    /**
     * @brief Increment total request count
     */
    void increment_requests() {
        total_requests_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Increment request count for a specific status code
     * 
     * @param status_code The HTTP status code (e.g., 200, 404, 500)
     */
    void increment_status_code(int status_code);

    /**
     * @brief Get total request count
     * 
     * @return uint64_t Total number of requests served
     */
    uint64_t total_requests() const {
        return total_requests_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get request count for a specific status code
     * 
     * @param status_code The HTTP status code
     * @return uint64_t Number of requests with this status code
     */
    uint64_t status_code_count(int status_code) const;

    // =========================================================================
    // Connection Metrics
    // =========================================================================

    /**
     * @brief Increment total connection count
     */
    void increment_connections() {
        total_connections_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Increment active connection count
     */
    void increment_active_connections() {
        active_connections_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Decrement active connection count
     */
    void decrement_active_connections() {
        active_connections_.fetch_sub(1, std::memory_order_relaxed);
    }

    /**
     * @brief Get total connection count
     * 
     * @return uint64_t Total number of connections accepted
     */
    uint64_t total_connections() const {
        return total_connections_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get active connection count
     * 
     * @return uint64_t Number of currently active connections
     */
    uint64_t active_connections() const {
        return active_connections_.load(std::memory_order_relaxed);
    }

    // =========================================================================
    // Error Metrics
    // =========================================================================

    /**
     * @brief Increment total error count
     */
    void increment_errors() {
        total_errors_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Increment error count for a specific error type
     * 
     * @param error_type The error type (e.g., "parse_error", "timeout", "io_error")
     */
    void increment_error_type(const std::string& error_type);

    /**
     * @brief Get total error count
     * 
     * @return uint64_t Total number of errors
     */
    uint64_t total_errors() const {
        return total_errors_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get error count for a specific error type
     * 
     * @param error_type The error type
     * @return uint64_t Number of errors of this type
     */
    uint64_t error_type_count(const std::string& error_type) const;

    // =========================================================================
    // Latency Metrics
    // =========================================================================

    /**
     * @brief Record a request latency (in microseconds)
     * 
     * @param latency_us The request latency in microseconds
     */
    void record_latency(uint64_t latency_us);

    /**
     * @brief Get p50 latency (median)
     * 
     * @return uint64_t 50th percentile latency in microseconds
     */
    uint64_t latency_p50() const;

    /**
     * @brief Get p95 latency
     * 
     * @return uint64_t 95th percentile latency in microseconds
     */
    uint64_t latency_p95() const;

    /**
     * @brief Get p99 latency
     * 
     * @return uint64_t 99th percentile latency in microseconds
     */
    uint64_t latency_p99() const;

    /**
     * @brief Get mean latency
     * 
     * @return uint64_t Mean latency in microseconds
     */
    uint64_t latency_mean() const;

    // =========================================================================
    // Byte Metrics
    // =========================================================================

    /**
     * @brief Add bytes sent
     * 
     * @param bytes Number of bytes sent
     */
    void add_bytes_sent(uint64_t bytes) {
        bytes_sent_.fetch_add(bytes, std::memory_order_relaxed);
    }

    /**
     * @brief Add bytes received
     * 
     * @param bytes Number of bytes received
     */
    void add_bytes_received(uint64_t bytes) {
        bytes_received_.fetch_add(bytes, std::memory_order_relaxed);
    }

    /**
     * @brief Get total bytes sent
     * 
     * @return uint64_t Total bytes sent
     */
    uint64_t bytes_sent() const {
        return bytes_sent_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get total bytes received
     * 
     * @return uint64_t Total bytes received
     */
    uint64_t bytes_received() const {
        return bytes_received_.load(std::memory_order_relaxed);
    }

    // =========================================================================
    // Parser Failure Metrics
    // =========================================================================

    /**
     * @brief Increment parser failure count
     */
    void increment_parser_failures() {
        parser_failures_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Get total parser failure count
     * 
     * @return uint64_t Total number of parser failures
     */
    uint64_t parser_failures() const {
        return parser_failures_.load(std::memory_order_relaxed);
    }

    // =========================================================================
    // Metrics Output
    // =========================================================================

    /**
     * @brief Export metrics in Prometheus-compatible format
     * 
     * Returns metrics in Prometheus text format:
     * # HELP aevrix_requests_total Total number of requests
     * # TYPE aevrix_requests_total counter
     * aevrix_requests_total 1234
     * 
     * @return std::string Metrics in Prometheus format
     */
    std::string export_prometheus() const;

    /**
     * @brief Export metrics in human-readable format
     * 
     * Returns metrics in a human-readable text format for debugging.
     * 
     * @return std::string Metrics in human-readable format
     */
    std::string export_text() const;

    /**
     * @brief Reset all metrics to zero
     * 
     * Useful for testing or manual metric resets.
     */
    void reset();

private:
    // =========================================================================
    // Member Variables
    // =========================================================================

    // Request metrics
    std::atomic<uint64_t> total_requests_;
    std::unordered_map<int, uint64_t> status_code_counts_;
    mutable std::mutex status_code_mutex_;

    // Connection metrics
    std::atomic<uint64_t> total_connections_;
    std::atomic<uint64_t> active_connections_;

    // Error metrics
    std::atomic<uint64_t> total_errors_;
    std::unordered_map<std::string, uint64_t> error_type_counts_;
    mutable std::mutex error_type_mutex_;

    // Latency metrics (simplified histogram)
    std::vector<uint64_t> latency_samples_;
    mutable std::mutex latency_mutex_;
    static constexpr size_t MAX_LATENCY_SAMPLES = 10000;

    // Byte metrics
    std::atomic<uint64_t> bytes_sent_;
    std::atomic<uint64_t> bytes_received_;

    // Parser failure metrics
    std::atomic<uint64_t> parser_failures_;
};

// =============================================================================
// Global Metrics Instance
// =============================================================================
// Global metrics instance accessible throughout the codebase.
// =============================================================================
extern Metrics g_metrics;

} // namespace aevrix
