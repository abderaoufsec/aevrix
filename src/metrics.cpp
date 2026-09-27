// =============================================================================
// Aevrix - Server Metrics Implementation
// =============================================================================
// This file implements the metrics collection system for server observability.
// =============================================================================

#include "aevrix/metrics.h"
#include <sstream>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <unordered_map>

namespace aevrix {

// =============================================================================
// Global Metrics Instance
// =============================================================================
Metrics g_metrics;

// =============================================================================
// Metrics Constructor
// =============================================================================

Metrics::Metrics()
    : total_requests_(0),
      total_connections_(0),
      active_connections_(0),
      total_errors_(0),
      bytes_sent_(0),
      bytes_received_(0),
      parser_failures_(0) {
    // Initialize common status codes
    std::vector<int> common_status_codes = {200, 201, 204, 206, 304, 400, 401, 403, 404, 405, 409, 413, 429, 500, 501, 502, 503};
    for (int code : common_status_codes) {
        status_code_counts_[code] = 0;
    }

    // Initialize common error types
    std::vector<std::string> common_error_types = {"parse_error", "timeout", "io_error", "connection_error", "configuration_error"};
    for (const auto& type : common_error_types) {
        error_type_counts_[type] = 0;
    }

    // Reserve space for latency samples
    latency_samples_.reserve(MAX_LATENCY_SAMPLES);
}

// =============================================================================
// Request Metrics Implementation
// =============================================================================

void Metrics::increment_status_code(int status_code) {
    std::lock_guard<std::mutex> lock(status_code_mutex_);
    status_code_counts_[status_code]++;
}

uint64_t Metrics::status_code_count(int status_code) const {
    std::lock_guard<std::mutex> lock(status_code_mutex_);
    auto it = status_code_counts_.find(status_code);
    if (it != status_code_counts_.end()) {
        return it->second;
    }
    return 0;
}

// =============================================================================
// Error Metrics Implementation
// =============================================================================

void Metrics::increment_error_type(const std::string& error_type) {
    std::lock_guard<std::mutex> lock(error_type_mutex_);
    error_type_counts_[error_type]++;
}

uint64_t Metrics::error_type_count(const std::string& error_type) const {
    std::lock_guard<std::mutex> lock(error_type_mutex_);
    auto it = error_type_counts_.find(error_type);
    if (it != error_type_counts_.end()) {
        return it->second;
    }
    return 0;
}

// =============================================================================
// Latency Metrics Implementation
// =============================================================================

void Metrics::record_latency(uint64_t latency_us) {
    std::lock_guard<std::mutex> lock(latency_mutex_);
    
    // Add latency sample
    latency_samples_.push_back(latency_us);
    
    // Keep only the most recent samples (circular buffer behavior)
    if (latency_samples_.size() > MAX_LATENCY_SAMPLES) {
        latency_samples_.erase(latency_samples_.begin());
    }
}

uint64_t Metrics::latency_p50() const {
    std::lock_guard<std::mutex> lock(latency_mutex_);
    
    if (latency_samples_.empty()) {
        return 0;
    }
    
    std::vector<uint64_t> sorted = latency_samples_;
    std::sort(sorted.begin(), sorted.end());
    
    size_t index = sorted.size() * 50 / 100;
    return sorted[index];
}

uint64_t Metrics::latency_p95() const {
    std::lock_guard<std::mutex> lock(latency_mutex_);
    
    if (latency_samples_.empty()) {
        return 0;
    }
    
    std::vector<uint64_t> sorted = latency_samples_;
    std::sort(sorted.begin(), sorted.end());
    
    size_t index = sorted.size() * 95 / 100;
    return sorted[index];
}

uint64_t Metrics::latency_p99() const {
    std::lock_guard<std::mutex> lock(latency_mutex_);
    
    if (latency_samples_.empty()) {
        return 0;
    }
    
    std::vector<uint64_t> sorted = latency_samples_;
    std::sort(sorted.begin(), sorted.end());
    
    size_t index = sorted.size() * 99 / 100;
    return sorted[index];
}

uint64_t Metrics::latency_mean() const {
    std::lock_guard<std::mutex> lock(latency_mutex_);
    
    if (latency_samples_.empty()) {
        return 0;
    }
    
    uint64_t sum = std::accumulate(latency_samples_.begin(), latency_samples_.end(), static_cast<uint64_t>(0));
    return sum / latency_samples_.size();
}

// =============================================================================
// Metrics Output Implementation
// =============================================================================

std::string Metrics::export_prometheus() const {
    std::stringstream ss;
    
    // Request metrics
    ss << "# HELP aevrix_requests_total Total number of requests\n";
    ss << "# TYPE aevrix_requests_total counter\n";
    ss << "aevrix_requests_total " << total_requests() << "\n\n";
    
    // Status code metrics
    ss << "# HELP aevrix_requests_by_status Total requests by status code\n";
    ss << "# TYPE aevrix_requests_by_status gauge\n";
    {
        std::lock_guard<std::mutex> lock(status_code_mutex_);
        for (const auto& [code, count] : status_code_counts_) {
            if (count > 0) {
                ss << "aevrix_requests_by_status{status_code=\"" << code << "\"} " << count << "\n";
            }
        }
    }
    ss << "\n";
    
    // Connection metrics
    ss << "# HELP aevrix_connections_total Total number of connections\n";
    ss << "# TYPE aevrix_connections_total counter\n";
    ss << "aevrix_connections_total " << total_connections() << "\n\n";
    
    ss << "# HELP aevrix_connections_active Number of active connections\n";
    ss << "# TYPE aevrix_connections_active gauge\n";
    ss << "aevrix_connections_active " << active_connections() << "\n\n";
    
    // Error metrics
    ss << "# HELP aevrix_errors_total Total number of errors\n";
    ss << "# TYPE aevrix_errors_total counter\n";
    ss << "aevrix_errors_total " << total_errors() << "\n\n";
    
    ss << "# HELP aevrix_errors_by_type Total errors by type\n";
    ss << "# TYPE aevrix_errors_by_type gauge\n";
    {
        std::lock_guard<std::mutex> lock(error_type_mutex_);
        for (const auto& [type, count] : error_type_counts_) {
            if (count > 0) {
                ss << "aevrix_errors_by_type{error_type=\"" << type << "\"} " << count << "\n";
            }
        }
    }
    ss << "\n";
    
    // Latency metrics
    ss << "# HELP aevrix_latency_p50 Request latency p50 (microseconds)\n";
    ss << "# TYPE aevrix_latency_p50 gauge\n";
    ss << "aevrix_latency_p50 " << latency_p50() << "\n\n";
    
    ss << "# HELP aevrix_latency_p95 Request latency p95 (microseconds)\n";
    ss << "# TYPE aevrix_latency_p95 gauge\n";
    ss << "aevrix_latency_p95 " << latency_p95() << "\n\n";
    
    ss << "# HELP aevrix_latency_p99 Request latency p99 (microseconds)\n";
    ss << "# TYPE aevrix_latency_p99 gauge\n";
    ss << "aevrix_latency_p99 " << latency_p99() << "\n\n";
    
    ss << "# HELP aevrix_latency_mean Request latency mean (microseconds)\n";
    ss << "# TYPE aevrix_latency_mean gauge\n";
    ss << "aevrix_latency_mean " << latency_mean() << "\n\n";
    
    // Byte metrics
    ss << "# HELP aevrix_bytes_sent_total Total bytes sent\n";
    ss << "# TYPE aevrix_bytes_sent_total counter\n";
    ss << "aevrix_bytes_sent_total " << bytes_sent() << "\n\n";
    
    ss << "# HELP aevrix_bytes_received_total Total bytes received\n";
    ss << "# TYPE aevrix_bytes_received_total counter\n";
    ss << "aevrix_bytes_received_total " << bytes_received() << "\n\n";
    
    // Parser failure metrics
    ss << "# HELP aevrix_parser_failures_total Total parser failures\n";
    ss << "# TYPE aevrix_parser_failures_total counter\n";
    ss << "aevrix_parser_failures_total " << parser_failures() << "\n\n";
    
    return ss.str();
}

std::string Metrics::export_text() const {
    std::stringstream ss;
    
    ss << "=== Aevrix Server Metrics ===\n\n";
    
    ss << "Requests:\n";
    ss << "  Total: " << total_requests() << "\n";
    ss << "  By Status Code:\n";
    {
        std::lock_guard<std::mutex> lock(status_code_mutex_);
        for (const auto& [code, count] : status_code_counts_) {
            if (count > 0) {
                ss << "    " << code << ": " << count << "\n";
            }
        }
    }
    ss << "\n";
    
    ss << "Connections:\n";
    ss << "  Total: " << total_connections() << "\n";
    ss << "  Active: " << active_connections() << "\n\n";
    
    ss << "Errors:\n";
    ss << "  Total: " << total_errors() << "\n";
    ss << "  By Type:\n";
    {
        std::lock_guard<std::mutex> lock(error_type_mutex_);
        for (const auto& [type, count] : error_type_counts_) {
            if (count > 0) {
                ss << "    " << type << ": " << count << "\n";
            }
        }
    }
    ss << "\n";
    
    ss << "Latency (microseconds):\n";
    ss << "  Mean: " << latency_mean() << "\n";
    ss << "  P50: " << latency_p50() << "\n";
    ss << "  P95: " << latency_p95() << "\n";
    ss << "  P99: " << latency_p99() << "\n\n";
    
    ss << "Bytes:\n";
    ss << "  Sent: " << bytes_sent() << "\n";
    ss << "  Received: " << bytes_received() << "\n\n";
    
    ss << "Parser Failures: " << parser_failures() << "\n\n";
    
    return ss.str();
}

void Metrics::reset() {
    total_requests_.store(0, std::memory_order_relaxed);
    total_connections_.store(0, std::memory_order_relaxed);
    active_connections_.store(0, std::memory_order_relaxed);
    total_errors_.store(0, std::memory_order_relaxed);
    bytes_sent_.store(0, std::memory_order_relaxed);
    bytes_received_.store(0, std::memory_order_relaxed);
    parser_failures_.store(0, std::memory_order_relaxed);
    
    {
        std::lock_guard<std::mutex> lock(status_code_mutex_);
        for (auto& [code, count] : status_code_counts_) {
            count = 0;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(error_type_mutex_);
        for (auto& [type, count] : error_type_counts_) {
            count = 0;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(latency_mutex_);
        latency_samples_.clear();
    }
}

} // namespace aevrix
