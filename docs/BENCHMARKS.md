# Aevrix Benchmark Documentation

## Overview

This document describes the benchmark harness for measuring performance characteristics of the Aevrix HTTP server. Benchmarks make performance measurable and reproducible, allowing performance optimization and regression detection.

## Benchmark Structure

Benchmarks are located in the `benchmarks/` directory:

```
benchmarks/
├── benchmark.h              # Benchmark utilities (timer, statistics, runner)
├── static_small.cpp         # Small static file serving benchmark (<10KB)
├── static_large.cpp         # Large static file serving benchmark (>1MB)
├── keepalive.cpp            # HTTP keep-alive connection benchmark
├── concurrent.cpp           # Concurrent request processing benchmark
└── parser.cpp               # HTTP request parser benchmark
```

## Benchmark Utilities

### BenchmarkTimer

High-resolution timer for measuring code execution time with nanosecond precision.

**Methods:**
- `start()`: Start the timer
- `stop()`: Stop the timer
- `elapsed_ns()`: Get elapsed time in nanoseconds
- `elapsed_us()`: Get elapsed time in microseconds
- `elapsed_ms()`: Get elapsed time in milliseconds
- `elapsed_s()`: Get elapsed time in seconds

### BenchmarkResult

Stores benchmark results including timing statistics.

**Fields:**
- `name`: Benchmark name
- `iterations`: Number of iterations
- `total_time_s`: Total time in seconds
- `mean_time_us`: Mean time per iteration (microseconds)
- `min_time_us`: Minimum time per iteration (microseconds)
- `max_time_us`: Maximum time per iteration (microseconds)
- `p50_time_us`: 50th percentile (microseconds)
- `p95_time_us`: 95th percentile (microseconds)
- `p99_time_us`: 99th percentile (microseconds)
- `ops_per_sec`: Operations per second

### BenchmarkRunner

Runs benchmarks multiple times and calculates statistics.

**Method:**
- `run(name, iterations, warmup_iterations, func)`: Run a benchmark function

**Parameters:**
- `name`: Benchmark name
- `iterations`: Number of iterations to run
- `warmup_iterations`: Number of warmup iterations (not counted)
- `func`: Function to benchmark (returns void, takes no arguments)

## Benchmarks

### Parser Benchmark (`parser.cpp`)

**Purpose**: Measures HTTP request parsing performance.

**Metrics:**
- Requests parsed per second
- Parse latency (mean, p50, p95, p99)
- Combined throughput for GET and POST requests

**Test Cases:**
- HTTP GET request with headers
- HTTP POST request with body

**Usage:**
```bash
./build/debug/benchmark_parser.exe
```

**Sample Output:**
```
=== HTTP GET Request Parsing ===
Iterations: 10000
Total time: 0.083 s
Ops/sec: 120192

Latency (microseconds):
  Mean: 8.32
  Min:  7.00
  Max:  60.00
  P50:  8.00
  P95:  9.00
  P99:  13.00

Combined Parser Throughput: 226615 req/sec
```

### Keep-Alive Benchmark (`keepalive.cpp`)

**Purpose**: Measures HTTP keep-alive connection performance.

**Metrics:**
- Response serialization ops/sec
- Serialization latency (mean, p50, p95, p99)

**Note**: This is a microbenchmark of response serialization. Full keep-alive benchmark would require actual TCP connection management.

**Usage:**
```bash
./build/debug/benchmark_keepalive.exe
```

**Sample Output:**
```
=== Keep-Alive Response Serialization ===
Iterations: 10000
Total time: 0.064 s
Ops/sec: 155224

Latency (microseconds):
  Mean: 6.44
  Min:  6.00
  Max:  290.00
  P50:  6.00
  P95:  7.00
  P99:  11.00
```

### Concurrent Benchmark (`concurrent.cpp`)

**Purpose**: Measures concurrent request processing performance.

**Metrics:**
- Requests processed per second
- Processing latency (mean, p50, p95, p99)
- Request counter

**Note**: This is a single-threaded simulation. Full concurrent benchmark would require actual multi-threaded server testing.

**Usage:**
```bash
./build/debug/benchmark_concurrent.exe
```

**Sample Output:**
```
=== Concurrent Request Processing ===
Iterations: 10000
Total time: 0.047 s
Ops/sec: 213547

Latency (microseconds):
  Mean: 4.68
  Min:  4.00
  Max:  61.00
  P50:  5.00
  P95:  5.00
  P99:  8.00

Processed requests: 10100
```

### Static Small File Benchmark (`static_small.cpp`)

**Purpose**: Measures performance of serving small static files (<10KB).

**Status**: Temporarily disabled due to StaticFileServer initialization issues in benchmark context.

**Metrics:**
- Files served per second
- Serve latency (mean, p50, p95, p99)
- Throughput

### Static Large File Benchmark (`static_large.cpp`)

**Purpose**: Measures performance of serving large static files (>1MB).

**Status**: Temporarily disabled due to StaticFileServer initialization issues in benchmark context.

**Metrics:**
- Files served per second
- Serve latency (mean, p50, p95, p99)
- Throughput (MB/s)

## Building Benchmarks

### Windows (MinGW)
```bash
cmake --preset debug-win
cmake --build --preset debug-win
```

### Linux/macOS
```bash
cmake --preset debug
cmake --build --preset debug
```

## Running Benchmarks

### Individual Benchmarks
```bash
./build/debug/benchmark_parser.exe
./build/debug/benchmark_keepalive.exe
./build/debug/benchmark_concurrent.exe
```

### All Benchmarks
```bash
cd build/debug
./benchmark_parser.exe
./benchmark_keepalive.exe
./benchmark_concurrent.exe
```

## Metrics Explained

### Requests per Second (ops/sec)
The number of operations completed per second. Higher is better.

### Latency Percentiles
- **Mean**: Average latency across all iterations
- **P50 (Median)**: 50th percentile - half of requests are faster, half are slower
- **P95**: 95th percentile - 95% of requests are faster, 5% are slower
- **P99**: 99th percentile - 99% of requests are faster, 1% are slower

Percentiles are more informative than mean/average because they show the tail latencies that affect user experience.

### Throughput
For large file benchmarks, throughput is measured in MB/s (megabytes per second).

## Environment Information

Each benchmark prints environment information:

```
=== Benchmark Environment ===
Platform: Windows
Compiler: GCC 16.1.0
C++ Standard: C++202002
Build Type: Debug
===========================
```

**Never publish a benchmark number without its environment.** Benchmark results are highly dependent on:
- Platform (Windows/Linux/macOS)
- Compiler version
- Build type (Debug/Release)
- CPU architecture
- RAM amount
- Disk speed (for file I/O benchmarks)

## Baselines

The roadmap suggests comparing:
- Aevrix blocking prototype
- Aevrix epoll version

**Do not compare against NGINX or other mature servers** without matching methodology and clearly explaining that the comparison is not an apples-to-apples product benchmark.

## Current Benchmark Results (Debug Build, Windows, GCC 16.1.0)

| Benchmark | Ops/sec | Mean Latency (μs) | P95 Latency (μs) | P99 Latency (μs) |
|-----------|---------|-------------------|------------------|------------------|
| Parser (GET) | 120,192 | 8.32 | 9.00 | 13.00 |
| Parser (POST) | 106,423 | 9.40 | 10.00 | 13.00 |
| Keep-Alive | 155,224 | 6.44 | 7.00 | 11.00 |
| Concurrent | 213,547 | 4.68 | 5.00 | 8.00 |

**Note**: These are debug build results. Release builds will be significantly faster (2x-10x).

## Benchmark Best Practices

1. **Run benchmarks in isolation**: Close other applications to avoid interference
2. **Use consistent hardware**: Benchmark on the same machine for comparison
3. **Run multiple times**: Take the median of 3-5 runs to account for variance
4. **Warm up first**: Allow CPU to reach stable frequency before measuring
5. **Document environment**: Always record platform, compiler, and build type
6. **Compare apples-to-apples**: Only compare same build type and platform
7. **Profile before optimizing**: Use profiling tools to identify bottlenecks
8. **Measure real workloads**: Synthetic benchmarks may not reflect real usage

## Interpreting Results

### High Ops/sec, Low Latency
- Good performance
- System is efficient
- Code is well-optimized

### High P99 Latency
- Indicates tail latency issues
- May be caused by:
  - Memory allocation
  - Lock contention
  - Cache misses
  - I/O operations

### High Variance (Max >> Min)
- Indicates inconsistent performance
- May be caused by:
  - CPU frequency scaling
  - Background processes
  - Cache effects
  - Branch prediction misses

## Future Work

1. **Enable static file benchmarks**: Fix StaticFileServer initialization issues
2. **Add real server benchmarks**: Benchmark actual server with HTTP clients
3. **Add network benchmarks**: Measure actual network throughput
4. **Add CPU profiling**: Use profiling tools to identify bottlenecks
5. **Add memory profiling**: Measure memory usage and allocation patterns
6. **Add concurrency benchmarks**: Test with actual multi-threaded server
7. **Add CI integration**: Run benchmarks in CI/CD pipeline
8. **Add performance regression detection**: Alert on performance degradation

## Resources

- [C++ Benchmarking Best Practices](https://github.com/google/benchmark)
- [Measuring Latency, Not Mean](https://www.ddbcopy.com/en/a-latency/)
- [The Problem with Single-Number Metrics](https://www.youtube.com/watch?v=lVTWqNLp7mM)
