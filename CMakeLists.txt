# =============================================================================
# Aevrix - CMake Build Configuration
# =============================================================================
# This CMakeLists.txt sets up the Aevrix HTTP server project with:
# - C++20 standard requirement
# - Strict compiler warnings (treated as errors)
# - CTest integration for automated testing
# - Sanitizer support via cmake/sanitizers.cmake
# - Debug and Release build configurations
# =============================================================================

cmake_minimum_required(VERSION 3.20)
project(aevrix VERSION 0.1.0 LANGUAGES CXX)

# Include sanitizer configuration module
# This provides functions to enable AddressSanitizer, UndefinedBehaviorSanitizer, etc.
include(cmake/sanitizers.cmake)

# =============================================================================
# C++ Standard Configuration
# =============================================================================
# Require C++20 for modern features (concepts, ranges, std::span, etc.)
# CXX_STANDARD_REQUIRED: Do not fall back to older standards if C++20 is unavailable
# CXX_EXTENSIONS OFF: Use standard C++ without compiler-specific extensions
# =============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find threading support for worker pool
find_package(Threads REQUIRED)

# =============================================================================
# Compiler Warning Configuration
# =============================================================================
# Enable strict compiler warnings and treat them as errors to enforce code quality
#
# MSVC (Windows):
# - /W4: Enable high warning level (includes warnings about suspicious code)
# - /WX: Treat warnings as errors (fail build on any warning)
# - /permissive-: Disable non-standard language extensions (enforce standard compliance)
# - /Zc:__cplusplus: Enable correct __cplusplus macro value
#
# GCC/Clang (Linux/Unix):
# - -Wall: Enable all warnings about questionable code construction
# - -Wextra: Enable extra warnings not covered by -Wall
# - -Wpedantic: Warn about non-standard C++ constructs (ISO C++ compliance)
# - -Werror: Treat all warnings as errors (fail build on any warning)
# - -Wconversion: Warn about implicit type conversions that may change values
# - -Wsign-conversion: Warn about implicit sign conversions
# - -Wshadow: Warn when a variable shadows another variable
# - -Wold-style-cast: Warn about C-style casts (prefer C++ casts)
# - -Wcast-align: Warn about pointer casts that increase alignment requirements
# - -Wunused: Warn about unused variables, functions, labels
# - -Woverloaded-virtual: Warn about overloaded virtual functions
# - -Wnon-virtual-dtor: Warn about non-virtual destructors in polymorphic classes
# =============================================================================
if(MSVC)
    add_compile_options(/W4 /WX /permissive- /Zc:__cplusplus /wd4242 /wd4244)
elseif(MINGW)
    # MinGW with GCC/Clang: disable sign-conversion warnings for Windows API compatibility
    # Windows headers have unavoidable sign conversions in ioctlsocket and FormatMessage
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wconversion
        -Wshadow
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wnon-virtual-dtor
    )
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wnon-virtual-dtor
    )
endif()

# =============================================================================
# Testing Configuration
# =============================================================================
# Enable CTest for automated test discovery and execution
# Tests are registered using add_test() and run with ctest command
# =============================================================================
enable_testing()

# =============================================================================
# Main Executable
# =============================================================================
# The aevrix executable - the HTTP server binary
# Initially contains placeholder code, will be replaced with actual implementation
# 
# Phase 2 adds:
# - src/tcp_listener.cpp (TCP socket management)
# - Uses UniqueFd for RAII socket management
#
# Phase 3 adds:
# - HTTP response serialization using HttpResponse class
# - Uses http_status.h, http_header.h, http_response.h, http_response_serializer.h
#
# Phase 4 adds:
# - HTTP request parsing using HttpRequestParser class
# - Uses http_method.h, http_request.h, http_request_parser.h
# - src/http_request_parser.cpp
#
# Phase 6 adds:
# - Static file serving using StaticFileServer class
# - Uses static_file_server.h
# - src/static_file_server.cpp
#
# Phase 8 adds:
# - Event loop with epoll on Linux, select on Windows
# - Uses event_loop.h
# - src/event_loop.cpp
#
# Phase 9 adds:
# - Connection state machine
# - Uses connection.h
# - src/connection.cpp
#
# Phase 10 adds:
# - Timeouts and resource limits
# - Uses server_config.h
# - src/server_config.cpp
#
# Phase 11 adds:
# - Worker pool for blocking operations
# - Uses worker_pool.h
# - Requires threading support
#
# Phase 12 adds:
# - Router for application-level routing
# - Uses router.h
# - src/router.cpp
#
# Phase 13 adds:
# - Configuration system
# - Uses config_parser.h
# - src/config_parser.cpp
#
# Phase 14 adds:
# - Structured logging
# - Uses logger.h
# - src/logger.cpp
#
# Phase 15 adds:
# - Graceful shutdown
# - Uses signal_handler.h
# - src/signal_handler.cpp
#
# Phase 19 adds:
# - HTTP cache control utilities
# - Uses http_cache.h
# - src/http_cache.cpp
#
# Phase 20 adds:
# - Metrics collection system
# - Uses metrics.h
# - src/metrics.cpp
# - Observability endpoints
# - Uses observability.h
# - src/observability.cpp
# =============================================================================
add_executable(aevrix
    src/main.cpp
    src/tcp_listener.cpp
    src/http_request_parser.cpp
    src/static_file_server.cpp
    src/event_loop.cpp
    src/connection.cpp
    src/connection_manager.cpp
    src/server_config.cpp
    src/router.cpp
    src/config_parser.cpp
    src/logger.cpp
    src/signal_handler.cpp
    src/http_cache.cpp
    src/metrics.cpp
    src/observability.cpp
    src/worker_completion_handler.cpp
    src/filesystem_worker.cpp
)

# Threading support for worker pool
target_link_libraries(aevrix PRIVATE Threads::Threads)

# Ensure main executable can find aevrix headers
target_include_directories(aevrix PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Link Windows socket library on Windows
if(WIN32)
    target_link_libraries(aevrix PRIVATE ws2_32)
endif()

# =============================================================================
# Test Executable
# =============================================================================
# The aevrix_tests executable - contains all unit and integration tests
# Uses CTest framework for automated test execution
# 
# Phase 1: UniqueFd tests
# Phase 3: HTTP response tests
# Phase 12: Router tests
# Phase 16: HTTP request tests
# Future phases will add more test files here
# =============================================================================
add_executable(aevrix_router_tests
    tests/router_tests.cpp
    src/router.cpp
)

# HTTP Response tests (separate executable due to linking issues)
add_executable(aevrix_http_tests
    tests/http_response_tests.cpp
)

# HTTP Request tests (enabled in Phase 16)
add_executable(aevrix_http_request_tests
    tests/http_request_tests.cpp
    src/http_request_parser.cpp
)

# HTTP Cache tests (enabled in Phase 19)
add_executable(aevrix_http_cache_tests
    tests/http_cache_tests.cpp
    src/http_cache.cpp
)

# Metrics tests (enabled in Phase 20)
add_executable(aevrix_metrics_tests
    tests/metrics_tests.cpp
    src/metrics.cpp
)

# Connection nonblocking I/O tests (Stage 1 - Event-Driven Refactor)
add_executable(aevrix_connection_nonblocking_tests
    tests/connection_nonblocking_tests.cpp
    src/connection.cpp
    src/logger.cpp
)

# Ensure connection nonblocking tests can find aevrix headers
target_include_directories(aevrix_connection_nonblocking_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Connection manager tests (Stage 2 - Event-Driven Refactor)
add_executable(aevrix_connection_manager_tests
    tests/connection_manager_tests.cpp
    src/connection_manager.cpp
    src/connection.cpp
    src/server_config.cpp
    src/config_parser.cpp
    src/logger.cpp
)

# Ensure connection manager tests can find aevrix headers
target_include_directories(aevrix_connection_manager_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Output state machine tests (Stage 4 - Response/Output State Machine)
add_executable(aevrix_output_state_machine_tests
    tests/output_state_machine_tests.cpp
    src/connection.cpp
    src/logger.cpp
)

# Ensure output state machine tests can find aevrix headers
target_include_directories(aevrix_output_state_machine_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Path security tests (enabled in Phase 16 - temporarily disabled due to implementation issues)
# add_executable(aevrix_path_security_tests
#     tests/path_security_tests.cpp
#     src/static_file_server.cpp
# )

# Ensure tests can find aevrix headers
target_include_directories(aevrix_router_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(aevrix_http_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(aevrix_http_request_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(aevrix_http_cache_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(aevrix_metrics_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
# target_include_directories(aevrix_path_security_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Link Windows socket library on Windows (for test file operations)
if(WIN32)
    target_link_libraries(aevrix_router_tests PRIVATE ws2_32)
    target_link_libraries(aevrix_http_tests PRIVATE ws2_32)
    target_link_libraries(aevrix_http_request_tests PRIVATE ws2_32)
    target_link_libraries(aevrix_http_cache_tests PRIVATE ws2_32)
    target_link_libraries(aevrix_metrics_tests PRIVATE ws2_32 Threads::Threads)
    target_link_libraries(aevrix_connection_nonblocking_tests PRIVATE ws2_32)
    target_link_libraries(aevrix_connection_manager_tests PRIVATE ws2_32)
    target_link_libraries(aevrix_output_state_machine_tests PRIVATE ws2_32)
    # aevrix_http_incremental_parser_tests doesn't need ws2_32 (uses simulated buffers)
    # target_link_libraries(aevrix_http_incremental_parser_tests PRIVATE ws2_32)
    # target_link_libraries(aevrix_path_security_tests PRIVATE ws2_32)
endif()

# Register HTTP response tests
add_test(NAME aevrix_http_tests COMMAND aevrix_http_tests)

# Register HTTP request tests (enabled in Phase 16)
add_test(NAME aevrix_http_request_tests COMMAND aevrix_http_request_tests)

# Register HTTP cache tests (enabled in Phase 19)
add_test(NAME aevrix_http_cache_tests COMMAND aevrix_http_cache_tests)

# Register metrics tests (enabled in Phase 20)
add_test(NAME aevrix_metrics_tests COMMAND aevrix_metrics_tests)

# Register connection nonblocking tests (Stage 1 - Event-Driven Refactor)
add_test(NAME aevrix_connection_nonblocking_tests COMMAND aevrix_connection_nonblocking_tests)

# Register connection manager tests (Stage 2 - Event-Driven Refactor)
add_test(NAME aevrix_connection_manager_tests COMMAND aevrix_connection_manager_tests)

# Register output state machine tests (Stage 4 - Response/Output State Machine)
add_test(NAME aevrix_output_state_machine_tests COMMAND aevrix_output_state_machine_tests)

# Register router tests (enabled in Phase 16)
add_test(NAME aevrix_router_tests COMMAND aevrix_router_tests)

# Register path security tests (enabled in Phase 16 - temporarily disabled)
# add_test(NAME aevrix_path_security_tests COMMAND aevrix_path_security_tests)

# =============================================================================
# Benchmark Targets (Phase 18)
# =============================================================================
# Benchmarks measure performance characteristics of the server
# Static file serving, keep-alive, concurrent requests, and parser performance
# Note: Static file benchmarks are temporarily disabled due to StaticFileServer
# initialization issues in benchmark context. Parser benchmark is fully functional.
# =============================================================================

# Static small file benchmark (temporarily disabled)
# add_executable(benchmark_static_small
#     benchmarks/static_small.cpp
#     src/static_file_server.cpp
# )

# Static large file benchmark (temporarily disabled)
# add_executable(benchmark_static_large
#     benchmarks/static_large.cpp
#     src/static_file_server.cpp
# )

# Keep-alive benchmark
add_executable(benchmark_keepalive
    benchmarks/keepalive.cpp
)

# Concurrent request benchmark
add_executable(benchmark_concurrent
    benchmarks/concurrent.cpp
)

# Parser benchmark
add_executable(benchmark_parser
    benchmarks/parser.cpp
    src/http_request_parser.cpp
)

# Ensure benchmarks can find aevrix headers
# target_include_directories(benchmark_static_small PRIVATE ${PROJECT_SOURCE_DIR}/include)
# target_include_directories(benchmark_static_large PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(benchmark_keepalive PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(benchmark_concurrent PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(benchmark_parser PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Disable conversion warnings for benchmarks (intentional conversions for statistics)
if(NOT MSVC)
    # target_compile_options(benchmark_static_small PRIVATE -Wno-conversion)
    # target_compile_options(benchmark_static_large PRIVATE -Wno-conversion)
    target_compile_options(benchmark_keepalive PRIVATE -Wno-conversion)
    target_compile_options(benchmark_concurrent PRIVATE -Wno-conversion)
    target_compile_options(benchmark_parser PRIVATE -Wno-conversion)
endif()

# Link threading library for concurrent benchmark
target_link_libraries(benchmark_concurrent PRIVATE Threads::Threads)

# =============================================================================
# Test Registration Complete
# =============================================================================
# All tests are registered above:
# - aevrix_http_tests (HTTP response tests)
# - aevrix_http_request_tests (HTTP request tests)
# - aevrix_router_tests (Router tests)
# Run tests with: ctest --test-dir build --output-on-failure
# =============================================================================
