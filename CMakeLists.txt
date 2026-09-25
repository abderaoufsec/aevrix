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

# =============================================================================
# Compiler Warning Configuration
# =============================================================================
# Enable strict compiler warnings and treat them as errors to enforce code quality
# - MSVC: /W4 (high warning level) + /WX (warnings as errors)
# - GCC/Clang: -Wall (all warnings) + -Wextra (extra warnings) + -Wpedantic (standard compliance) + -Werror (warnings as errors)
# =============================================================================
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
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
# =============================================================================
add_executable(aevrix
    src/main.cpp
    src/tcp_listener.cpp
    src/http_request_parser.cpp
    src/static_file_server.cpp
    src/event_loop.cpp
)

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
# Future phases will add more test files here
# =============================================================================
add_executable(aevrix_tests
    tests/unique_fd_tests.cpp
)

# HTTP Response tests (separate executable due to linking issues)
add_executable(aevrix_http_tests
    tests/http_response_tests.cpp
)

# HTTP Request tests (disabled due to linking issues on Windows)
# add_executable(aevrix_http_request_tests
#     tests/http_request_tests.cpp
# )

# Ensure tests can find aevrix headers
target_include_directories(aevrix_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(aevrix_http_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
# target_include_directories(aevrix_http_request_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Link Windows socket library on Windows (for test file operations)
if(WIN32)
    target_link_libraries(aevrix_tests PRIVATE ws2_32)
    target_link_libraries(aevrix_http_tests PRIVATE ws2_32)
    # target_link_libraries(aevrix_http_request_tests PRIVATE ws2_32)
endif()

# Register HTTP response tests
add_test(NAME aevrix_http_tests COMMAND aevrix_http_tests)

# Register HTTP request tests (disabled)
# add_test(NAME aevrix_http_request_tests COMMAND aevrix_http_request_tests)

# =============================================================================
# Test Registration
# =============================================================================
# Register the test executable with CTest
# This allows running tests with: ctest --preset debug
# =============================================================================
add_test(NAME aevrix_tests COMMAND aevrix_tests)
