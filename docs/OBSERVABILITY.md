# Observability (Phase 20)

## Overview

Phase 20 adds observability features to the Aevrix server, enabling developers to understand server behavior without attaching a debugger. Observability is achieved through metrics collection and HTTP endpoints for monitoring and diagnostics.

## Features Implemented

### 1. Metrics Collection System

**Purpose**: Thread-safe metrics collection for tracking server behavior at runtime.

**Implementation**:
- `Metrics` class with atomic counters for thread-safe updates
- Request metrics (total requests, requests by status code)
- Connection metrics (total connections, active connections)
- Error metrics (total errors, errors by type)
- Latency metrics (p50, p95, p99, mean)
- Byte metrics (bytes sent, bytes received)
- Parser failure metrics (total parser failures)

**Usage**:
```cpp
// Increment request count
aevrix::g_metrics.increment_requests();

// Increment status code
aevrix::g_metrics.increment_status_code(200);

// Record latency
aevrix::g_metrics.record_latency(1234);  // microseconds

// Add bytes
aevrix::g_metrics.add_bytes_sent(1024);
aevrix::g_metrics.add_bytes_received(512);
```

### 2. /metrics Endpoint

**Purpose**: Exposes metrics in Prometheus-compatible format for scraping by monitoring systems.

**Implementation**:
- `handle_metrics_endpoint()` function
- Returns metrics in Prometheus text format
- Includes HELP and TYPE comments for Prometheus compatibility
- Content-Type: `text/plain; version=0.0.4`

**Usage**:
```bash
curl http://localhost:8080/metrics
```

**Sample Output**:
```
# HELP aevrix_requests_total Total number of requests
# TYPE aevrix_requests_total counter
aevrix_requests_total 1234

# HELP aevrix_connections_total Total number of connections
# TYPE aevrix_connections_total counter
aevrix_connections_total 567

# HELP aevrix_connections_active Number of active connections
# TYPE aevrix_connections_active gauge
aevrix_connections_active 12

# HELP aevrix_latency_p50 Request latency p50 (microseconds)
# TYPE aevrix_latency_p50 gauge
aevrix_latency_p50 842

# HELP aevrix_latency_p95 Request latency p95 (microseconds)
# TYPE aevrix_latency_p95 gauge
aevrix_latency_p95 1234

# HELP aevrix_latency_p99 Request latency p99 (microseconds)
# TYPE aevrix_latency_p99 gauge
aevrix_latency_p99 2345
```

### 3. /health Endpoint

**Purpose**: Health check endpoint for uptime monitoring and load balancer health checks.

**Implementation**:
- `handle_health_endpoint()` function
- Returns server health status
- Includes active connection count
- Simple "OK" response for basic health checks

**Usage**:
```bash
curl http://localhost:8080/health
```

**Sample Output**:
```
OK
status: healthy
active_connections: 12
```

### 4. /server-info Endpoint

**Purpose**: Server information endpoint for version and configuration details.

**Implementation**:
- `handle_server_info_endpoint()` function
- Returns server version and current metrics
- Useful for debugging and server identification

**Usage**:
```bash
curl http://localhost:8080/server-info
```

**Sample Output**:
```
Aevrix HTTP Server
Version: 1.0.0
HTTP Version: HTTP/1.1
C++ Standard: C++20

Metrics:
  Total Requests: 1234
  Total Connections: 567
  Active Connections: 12
  Total Errors: 5
  Bytes Sent: 1048576
  Bytes Received: 512000
  Parser Failures: 2
```

## Metrics Tracked

### Request Metrics

- **Total Requests**: Total number of HTTP requests processed
- **Requests by Status Code**: Count of requests for each HTTP status code (200, 404, 500, etc.)

**Use Cases**:
- Monitoring request rate
- Tracking error rates (4xx, 5xx status codes)
- Identifying spikes in traffic

### Connection Metrics

- **Total Connections**: Total number of TCP connections accepted
- **Active Connections**: Number of currently active connections

**Use Cases**:
- Monitoring connection rate
- Tracking concurrent connections
- Identifying connection leaks

### Error Metrics

- **Total Errors**: Total number of errors
- **Errors by Type**: Count of errors by type (parse_error, timeout, io_error, etc.)

**Use Cases**:
- Monitoring error rates
- Identifying error patterns
- Debugging specific error types

### Latency Metrics

- **Mean Latency**: Average request latency in microseconds
- **P50 Latency**: Median latency (50th percentile)
- **P95 Latency**: 95th percentile latency
- **P99 Latency**: 99th percentile latency

**Use Cases**:
- Monitoring response time
- Identifying slow requests
- Tracking latency percentiles (more informative than mean)

**Note**: Latency samples are stored in a circular buffer with a maximum of 10,000 samples.

### Byte Metrics

- **Bytes Sent**: Total bytes sent to clients
- **Bytes Received**: Total bytes received from clients

**Use Cases**:
- Monitoring bandwidth usage
- Tracking data transfer volumes
- Identifying bandwidth spikes

### Parser Failure Metrics

- **Parser Failures**: Total number of HTTP request parsing failures

**Use Cases**:
- Monitoring malformed requests
- Identifying client compatibility issues
- Debugging parser errors

## Integration with Main Server

To integrate observability with the main server:

```cpp
#include "aevrix/observability.h"

// In main.cpp, after router initialization
aevrix::observability::register_observability_endpoints(router);

// When handling requests, increment metrics
aevrix::g_metrics.increment_requests();
aevrix::g_metrics.increment_status_code(response.status());

// When accepting connections
aevrix::g_metrics.increment_connections();
aevrix::g_metrics.increment_active_connections();

// When closing connections
aevrix::g_metrics.decrement_active_connections();

// When recording latency
auto start = std::chrono::high_resolution_clock::now();
// ... process request ...
auto end = std::chrono::high_resolution_clock::now();
auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
aevrix::g_metrics.record_latency(latency_us);

// When sending/receiving data
aevrix::g_metrics.add_bytes_sent(bytes_sent);
aevrix::g_metrics.add_bytes_received(bytes_received);

// When parser fails
aevrix::g_metrics.increment_parser_failures();
```

## Thread Safety

All metrics use atomic operations or mutex locks to ensure thread-safe updates from multiple threads:

- Simple counters use `std::atomic<uint64_t>` for lock-free updates
- Complex data structures (maps, vectors) use `std::mutex` for protection
- No race conditions or data corruption in multi-threaded environments

**Thread Safety Test**:
- 10 threads each increment metrics 1,000 times
- Final counts verified to be exactly 10,000
- No lost increments or corruption

## Performance Considerations

### Overhead

- Atomic operations have minimal overhead (single CPU instruction)
- Mutex locks only for complex operations (maps, vectors)
- Latency sampling limited to 10,000 samples (circular buffer)
- Metrics export is lazy (only when requested)

### Recommendations

- Increment metrics in hot paths (request handling)
- Export metrics periodically (e.g., every 10 seconds)
- Use Prometheus for scraping and long-term storage
- Consider aggregating metrics across multiple instances

## Prometheus Integration

### Scrape Configuration

Add to `prometheus.yml`:

```yaml
scrape_configs:
  - job_name: 'aevrix'
    static_configs:
      - targets: ['localhost:8080']
    metrics_path: '/metrics'
    scrape_interval: 15s
```

### Grafana Dashboard

Create a Grafana dashboard with panels for:
- Request rate (requests per second)
- Error rate (errors per second)
- P95 latency
- Active connections
- Bytes sent/received

Example PromQL queries:

```promql
# Request rate
rate(aevrix_requests_total[5m])

# Error rate
rate(aevrix_errors_total[5m])

# P95 latency
aevrix_latency_p95

# Active connections
aevrix_connections_active
```

## Testing

### Unit Tests

Unit tests cover:
- Request metrics (total, status codes)
- Connection metrics (total, active)
- Error metrics (total, by type)
- Latency metrics (mean, p50, p95, p99)
- Byte metrics (sent, received)
- Parser failure metrics
- Prometheus export format
- Text export format
- Metrics reset
- Thread safety (10 threads × 1,000 iterations)

Run unit tests:
```bash
ctest --test-dir build/debug --output-on-failure
```

### Test Coverage

- Request metrics: 3 tests
- Connection metrics: 2 tests
- Error metrics: 3 tests
- Latency metrics: 4 tests
- Byte metrics: 2 tests
- Parser failure metrics: 1 test
- Export functions: 2 tests
- Reset function: 1 test
- Thread safety: 1 test

**Total: 19 unit tests**

## Security Considerations

### Metrics Exposure

- Metrics endpoints should be protected in production
- Consider authentication or network-level restrictions
- Metrics may reveal sensitive information (traffic patterns, error rates)

### Recommendations

- Block `/metrics`, `/health`, `/server-info` from public internet
- Use firewall rules or reverse proxy authentication
- Consider separate monitoring network
- Audit metrics for sensitive information

## Debugging with Observability

### Without Metrics

```cpp
// Attach debugger
// Set breakpoints
// Step through code
// Check variables
// Time-consuming and intrusive
```

### With Metrics

```bash
# Check metrics endpoint
curl http://localhost:8080/metrics

# Check health
curl http://localhost:8080/health

# Check server info
curl http://localhost:8080/server-info

# Quick, non-intrusive, continuous monitoring
```

### Common Debugging Scenarios

**High Error Rate**:
```bash
# Check error metrics
curl http://localhost:8080/metrics | grep errors
# Identify error type
# Investigate root cause
```

**High Latency**:
```bash
# Check latency metrics
curl http://localhost:8080/metrics | grep latency
# Identify if P99 is high
# Profile slow paths
```

**Connection Issues**:
```bash
# Check connection metrics
curl http://localhost:8080/metrics | grep connections
# Check if active connections are increasing
# Investigate connection leaks
```

## Future Enhancements

1. **Histogram Support**: More sophisticated latency distribution tracking
2. **Custom Metrics**: Allow user-defined metrics
3. **Metrics Labels**: Support for labeled metrics (e.g., by endpoint)
4. **Metrics Aggregation**: Aggregate metrics across multiple instances
5. **Dynamic Configuration**: Configure which metrics to track
6. **Push Gateway**: Support for Prometheus push gateway
7. **OpenTelemetry**: OpenTelemetry metrics export
8. **Structured Logging**: Correlate logs with metrics

## Best Practices

1. **Increment Early**: Increment metrics as early as possible in request handling
2. **Increment Consistently**: Ensure all code paths increment metrics appropriately
3. **Use Percentiles**: Use p95/p99 latency instead of mean for performance analysis
4. **Monitor Continuously**: Set up continuous monitoring with Prometheus/Grafana
5. **Set Alerts**: Configure alerts for error rates, latency, and connection limits
6. **Document Metrics**: Document what each metric represents and how to interpret it
7. **Test Metrics**: Include metrics tests in CI/CD pipeline
8. **Review Regularly**: Review metrics regularly to identify trends and issues

## References

- Prometheus Documentation: https://prometheus.io/docs/
- Grafana Documentation: https://grafana.com/docs/
- Observability Best Practices: https://sre.google/sre-book/monitoring-distributed-systems/
