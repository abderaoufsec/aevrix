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
project(aevrix VERSION 1.0.0 LANGUAGES CXX)

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
# OpenSSL Support (Phase 21 - TLS)
# =============================================================================
# OpenSSL is required for TLS support. If TLS is enabled, OpenSSL must be found.
# The project can still build without TLS if OpenSSL is not available.
# =============================================================================
option(ENABLE_TLS "Enable TLS support" ON)

if(ENABLE_TLS)
    find_package(OpenSSL)
    if(OPENSSL_FOUND)
        message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")
        message(STATUS "OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
        message(STATUS "OpenSSL libraries: ${OPENSSL_LIBRARIES}")
    else()
        message(WARNING "TLS support requested but OpenSSL not found. "
                   "TLS will be disabled. Install OpenSSL development packages (e.g., libssl-dev on Debian/Kali) to enable TLS.")
        set(ENABLE_TLS OFF)
    endif()
else()
    message(STATUS "TLS support disabled")
endif()

# =============================================================================
# GoogleTest Dependency (GTest)
# =============================================================================
# NOTE: This block must stay BEFORE the "Compiler Warning Configuration"
# section below. The project sets strict global warnings (-Werror etc.) via
# add_compile_options(), and directory compile options propagate into
# FetchContent sub-builds. Configuring googletest first keeps its own targets
# free of our -Werror flags.
#
# TLS unit tests use GoogleTest (#include <gtest/gtest.h>). CI runners
# (ubuntu-latest) do not ship GTest headers, so a bare `find_package(GTest)`
# fails there with "gtest/gtest.h: No such file or directory".
#
# Strategy (preferred: self-contained, works on CI + local checkouts):
#   1. Try `find_package(GTest QUIET)` first so system packages are reused
#      when present (faster configure, distro-patched builds).
#   2. Fall back to CMake FetchContent to download and build the pinned
#      googletest release. This needs only network access at configure time;
#      no `apt install libgtest-dev` or manual `cmake --build` of /usr/src/gtest.
#
# To force the fallback (e.g. hermetic/offline-mirror testing):
#   cmake -DAEVRIX_USE_SYSTEM_GTEST=OFF ...
# To force system-only (fail fast when GTest is absent):
#   cmake -DAEVRIX_FETCH_GTEST=OFF ...
# =============================================================================
option(AEVRIX_USE_SYSTEM_GTEST "Prefer system-installed GTest when available" ON)
option(AEVRIX_FETCH_GTEST "Fetch GoogleTest via FetchContent when not found on the system" ON)

if(AEVRIX_USE_SYSTEM_GTEST)
    find_package(GTest QUIET)
endif()

if(NOT GTest_FOUND AND AEVRIX_FETCH_GTEST)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    include(FetchContent)
    # Keep googletest's own build quiet and consistent with the parent project:
    # shared CRT on Windows (avoids LNK2038), no gmock, no install rules.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    )
    FetchContent_MakeAvailable(googletest)
    # The legacy FindGTest module provides GTest::GTest / GTest::Main
    # (capitalized), while the FetchContent build provides the real targets
    # `gtest` / `gtest_main` (plus upstream's own GTest::gtest aliases).
    # Alias targets cannot point at other ALIAS targets, so bind the
    # capitalized names to the real targets. All test targets below then link
    # uniformly against GTest::GTest / GTest::Main.
    if(NOT TARGET GTest::GTest AND TARGET gtest)
        add_library(GTest::GTest ALIAS gtest)
    endif()
    if(NOT TARGET GTest::Main AND TARGET gtest_main)
        add_library(GTest::Main ALIAS gtest_main)
    endif()
    # Mark as found so the TLS test targets below are configured.
    set(GTest_FOUND TRUE)
    message(STATUS "GTest provided via FetchContent (googletest v1.15.2)")
endif()

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

if(GTest_FOUND)
    message(STATUS "GTest found - TLS unit tests (GTest-based) will be built")
else()
    message(WARNING "GTest not found and AEVRIX_FETCH_GTEST is OFF - "
                    "GTest-based TLS test targets will be skipped. "
                    "Re-enable AEVRIX_FETCH_GTEST or install GTest.")
endif()

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
    src/server_config_store.cpp
    src/http_cache.cpp
    src/metrics.cpp
    src/observability.cpp
    src/worker_completion_handler.cpp
    src/filesystem_worker.cpp
)

# Phase 22 adds reverse-proxy support. The runtime pieces use POSIX sockets and
# the epoll event loop, so they are built on non-Windows platforms only; the
# parsing/rewriting components are portable and always built.
target_sources(aevrix PRIVATE
    src/proxy_target.cpp
    src/proxy_request_builder.cpp
    src/proxy_response_parser.cpp
)

# Phase 23 adds WebSocket support. The framing/handshake/state-machine layers
# are portable (no sockets), so they build on every platform; the epoll-driven
# upgrade path lives in main.cpp like the rest of the event loop.
target_sources(aevrix PRIVATE
    src/websocket_frame.cpp
    src/websocket_handshake.cpp
    src/websocket_connection.cpp
)

if(NOT WIN32)
    target_sources(aevrix PRIVATE
        src/upstream_pool.cpp
        src/proxy_handler.cpp
    )
endif()

# Add TLS sources only if TLS is enabled
if(ENABLE_TLS AND OPENSSL_FOUND)
    target_sources(aevrix PRIVATE
        src/tls_context.cpp
        src/tls_connection.cpp
    )
endif()

# Threading support for worker pool
target_link_libraries(aevrix PRIVATE Threads::Threads)

# Link OpenSSL if TLS is enabled
if(ENABLE_TLS AND OPENSSL_FOUND)
    target_link_libraries(aevrix PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_include_directories(aevrix PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_compile_definitions(aevrix PRIVATE AEVRIX_ENABLE_TLS)
endif()

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
    src/websocket_connection.cpp
    src/websocket_frame.cpp
    # Phase 24: ConnectionManager reads limits through the config store.
    src/server_config_store.cpp
)

# Ensure connection manager tests can find aevrix headers
target_include_directories(aevrix_connection_manager_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Configuration reload tests (Phase 24 - atomic config store / SIGHUP reload).
# Links the store plus its consumers (ConnectionManager) and the signal handler,
# so the atomic swap and the wake-up path are exercised for real.
add_executable(aevrix_config_reload_tests
    tests/config_reload_tests.cpp
    src/server_config_store.cpp
    src/server_config.cpp
    src/config_parser.cpp
    src/logger.cpp
    src/signal_handler.cpp
    src/connection_manager.cpp
    src/connection.cpp
    src/websocket_connection.cpp
    src/websocket_frame.cpp
)

# Ensure configuration reload tests can find aevrix headers
target_include_directories(aevrix_config_reload_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Output state machine tests (Stage 4 - Response/Output State Machine)
add_executable(aevrix_output_state_machine_tests
    tests/output_state_machine_tests.cpp
    src/connection.cpp
    src/logger.cpp
)

# Ensure output state machine tests can find aevrix headers
target_include_directories(aevrix_output_state_machine_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Timeout enforcement tests (Stage 6 - Timeout Enforcement)
add_executable(aevrix_timeout_enforcement_tests
    tests/timeout_enforcement_tests.cpp
    src/connection.cpp
    src/server_config.cpp
    src/config_parser.cpp
    src/logger.cpp
)

# Ensure timeout enforcement tests can find aevrix headers
target_include_directories(aevrix_timeout_enforcement_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Path security tests (Stage 7 - Static-File Security Audit)
add_executable(aevrix_path_security_tests
    tests/path_security_tests.cpp
    src/static_file_server.cpp
    src/logger.cpp
)

# Ensure path security tests can find aevrix headers
target_include_directories(aevrix_path_security_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Reverse proxy tests (Phase 22)
add_executable(aevrix_proxy_target_tests
    tests/proxy_target_tests.cpp
    src/proxy_target.cpp
)

add_executable(aevrix_proxy_request_builder_tests
    tests/proxy_request_builder_tests.cpp
    src/proxy_request_builder.cpp
    src/proxy_target.cpp
    src/logger.cpp
)

add_executable(aevrix_proxy_response_parser_tests
    tests/proxy_response_parser_tests.cpp
    src/proxy_response_parser.cpp
    src/logger.cpp
)

# Ensure proxy tests can find aevrix headers
target_include_directories(aevrix_proxy_target_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(aevrix_proxy_request_builder_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(aevrix_proxy_response_parser_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

# The upstream pool is POSIX-only (like the proxy runtime itself), so its test
# - which drives real loopback sockets from an acceptor thread - is too.
if(NOT WIN32)
    add_executable(aevrix_upstream_pool_tests
        tests/upstream_pool_tests.cpp
        src/upstream_pool.cpp
        src/proxy_target.cpp
    )

    target_include_directories(aevrix_upstream_pool_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

    # The pool test spawns an acceptor thread and opens real loopback sockets
    target_link_libraries(aevrix_upstream_pool_tests PRIVATE Threads::Threads)
endif()

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
    target_link_libraries(aevrix_timeout_enforcement_tests PRIVATE ws2_32)
    target_link_libraries(aevrix_path_security_tests PRIVATE ws2_32)
    # aevrix_http_incremental_parser_tests doesn't need ws2_32 (uses simulated buffers)
    # target_link_libraries(aevrix_http_incremental_parser_tests PRIVATE ws2_32)
    if(ENABLE_TLS AND OPENSSL_FOUND)
        target_link_libraries(aevrix_tls_context_tests PRIVATE ws2_32)
        target_link_libraries(aevrix_tls_connection_tests PRIVATE ws2_32)
        target_link_libraries(aevrix_tls_integration_tests PRIVATE ws2_32)
    endif()
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

# Register configuration reload tests (Phase 24 - atomic config reload)
add_test(NAME aevrix_config_reload_tests COMMAND aevrix_config_reload_tests)

# Register output state machine tests (Stage 4 - Response/Output State Machine)
add_test(NAME aevrix_output_state_machine_tests COMMAND aevrix_output_state_machine_tests)

# Register timeout enforcement tests (Stage 6 - Timeout Enforcement)
add_test(NAME aevrix_timeout_enforcement_tests COMMAND aevrix_timeout_enforcement_tests)

# Register path security tests (Stage 7 - Static-File Security Audit)
# Skipped on Windows (requires Linux filesystem)
if(UNIX)
    add_test(NAME aevrix_path_security_tests COMMAND aevrix_path_security_tests)
endif()

# Register reverse proxy tests (Phase 22)
add_test(NAME aevrix_proxy_target_tests COMMAND aevrix_proxy_target_tests)
add_test(NAME aevrix_proxy_request_builder_tests COMMAND aevrix_proxy_request_builder_tests)
add_test(NAME aevrix_proxy_response_parser_tests COMMAND aevrix_proxy_response_parser_tests)

if(NOT WIN32)
    add_test(NAME aevrix_upstream_pool_tests COMMAND aevrix_upstream_pool_tests)
endif()

# WebSocket frame/connection tests (Phase 23)
add_executable(aevrix_websocket_frame_tests
    tests/websocket_frame_tests.cpp
    src/websocket_frame.cpp
    src/websocket_connection.cpp
)
target_include_directories(aevrix_websocket_frame_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
if(WIN32)
    target_link_libraries(aevrix_websocket_frame_tests PRIVATE ws2_32)
endif()
add_test(NAME aevrix_websocket_frame_tests COMMAND aevrix_websocket_frame_tests)

# WebSocket upgrade handshake tests (Phase 23)
add_executable(aevrix_websocket_handshake_tests
    tests/websocket_handshake_tests.cpp
    src/websocket_handshake.cpp
    src/server_config.cpp
    src/config_parser.cpp
    src/logger.cpp
)
target_include_directories(aevrix_websocket_handshake_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
if(WIN32)
    target_link_libraries(aevrix_websocket_handshake_tests PRIVATE ws2_32)
endif()
add_test(NAME aevrix_websocket_handshake_tests COMMAND aevrix_websocket_handshake_tests)

# Register TLS tests (Phase 21)
# These tests are GTest-based, so they need both OpenSSL and GoogleTest.
# GTest_FOUND is guaranteed on CI/local builds via find_package + FetchContent
# fallback above; the guard keeps configuration working when GTest is
# deliberately unavailable (AEVRIX_USE_SYSTEM_GTEST=OFF + AEVRIX_FETCH_GTEST=OFF).
if(ENABLE_TLS AND OPENSSL_FOUND AND GTest_FOUND)
    add_executable(aevrix_tls_context_tests
        tests/tls_context_tests.cpp
        src/tls_context.cpp
        src/logger.cpp
    )
    target_include_directories(aevrix_tls_context_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_compile_definitions(aevrix_tls_context_tests PRIVATE AEVRIX_ENABLE_TLS)
    # GTest::GTest / GTest::Main resolve to the system package or to the
    # FetchContent aliases declared above -- always present when GTest_FOUND.
    target_link_libraries(aevrix_tls_context_tests PRIVATE GTest::GTest GTest::Main)
    target_link_libraries(aevrix_tls_context_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    add_test(NAME aevrix_tls_context_tests COMMAND aevrix_tls_context_tests)

    add_executable(aevrix_tls_connection_tests
        tests/tls_connection_tests.cpp
        src/tls_connection.cpp
        src/tls_context.cpp
        src/logger.cpp
    )
    target_include_directories(aevrix_tls_connection_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_compile_definitions(aevrix_tls_connection_tests PRIVATE AEVRIX_ENABLE_TLS)
    target_link_libraries(aevrix_tls_connection_tests PRIVATE GTest::GTest GTest::Main)
    target_link_libraries(aevrix_tls_connection_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    add_test(NAME aevrix_tls_connection_tests COMMAND aevrix_tls_connection_tests)

    add_executable(aevrix_tls_integration_tests
        tests/tls_integration_tests.cpp
        src/tls_context.cpp
        src/tls_connection.cpp
        src/logger.cpp
    )
    target_include_directories(aevrix_tls_integration_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_compile_definitions(aevrix_tls_integration_tests PRIVATE AEVRIX_ENABLE_TLS)
    target_link_libraries(aevrix_tls_integration_tests PRIVATE GTest::GTest GTest::Main)
    target_link_libraries(aevrix_tls_integration_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    add_test(NAME aevrix_tls_integration_tests COMMAND aevrix_tls_integration_tests)
endif()

# Register router tests (enabled in Phase 16)
add_test(NAME aevrix_router_tests COMMAND aevrix_router_tests)

# =============================================================================
# Keep Assertions Active In Test Builds
# =============================================================================
# Most test suites are built on <cassert> (a few use the in-file TEST_ASSERT
# macro). Release builds define NDEBUG, which turns every assert() into a
# no-op. That both silently disables the checks and breaks the build under
# -Werror, because variables only consumed by an assert() become
# "unused"/"used uninitialized" (e.g. tests/http_cache_tests.cpp).
#
# Undefining NDEBUG for test targets only keeps Release runs checking exactly
# what Debug runs check. The aevrix server binary keeps NDEBUG (standard
# production behaviour). Must run after all test targets are declared.
# =============================================================================
get_property(aevrix_directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(aevrix_target IN LISTS aevrix_directory_targets)
    if(aevrix_target MATCHES "_tests$")
        if(MSVC)
            target_compile_options(${aevrix_target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${aevrix_target} PRIVATE -UNDEBUG)
        endif()
    endif()
endforeach()

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
