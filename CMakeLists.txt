cmake_minimum_required(VERSION 3.20)
project(aevrix VERSION 0.1.0 LANGUAGES CXX)

# Include sanitizer configuration
include(cmake/sanitizers.cmake)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Enable testing
enable_testing()

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Main executable
add_executable(aevrix
    src/main.cpp
)

# Test executable
add_executable(aevrix_tests
    tests/main.cpp
)

# Register tests with CTest
add_test(NAME aevrix_tests COMMAND aevrix_tests)
