# =============================================================================
# Aevrix - Sanitizer Configuration Module
# =============================================================================
# This CMake module provides functions to enable various memory and thread
# sanitizers for the Aevrix project. Sanitizers help detect bugs that ordinary
# tests miss, including:
#
# Memory Errors (AddressSanitizer):
# - Memory leaks (memory allocated but never freed)
# - Buffer overflows (reading/writing past array bounds)
# - Use-after-free (accessing memory after it has been freed)
# - Double free (freeing the same memory twice)
# - Stack buffer overflow (corrupting the stack)
# - Heap buffer overflow (corrupting the heap)
# - Use-after-return (accessing stack memory after function returns)
# - Use-after-scope (accessing variable after it goes out of scope)
#
# Undefined Behavior (UndefinedBehaviorSanitizer):
# - Integer overflow (signed integer arithmetic overflow)
# - Misaligned pointer access (accessing misaligned memory)
# - Null pointer dereference (dereferencing null pointers)
# - Signed integer overflow (undefined behavior in C++)
# - Shift count out of range (shifting by more than bit width)
# - Invalid enum values (using invalid enum values)
# - Floating point cast overflow (float to int overflow)
# - Division by zero (integer division by zero)
#
# Threading Issues (ThreadSanitizer):
# - Data races (concurrent access to shared memory without synchronization)
# - Lock order inversion (potential deadlocks)
# - Thread leaks (threads not joined before exit)
# - Signal handler races (unsafe signal handler usage)
#
# Usage:
#   include(cmake/sanitizers.cmake)
#   enable_sanitizer(target_name "address")
#   enable_all_sanitizers(target_name)
#
# Platform Limitations:
# - Sanitizers are primarily supported on Linux with GCC/Clang
# - MSVC (Windows) has limited sanitizer support and is disabled by default
# - MinGW on Windows has very limited sanitizer support
# - ThreadSanitizer has significant performance overhead (10x-20x slowdown)
# - AddressSanitizer has moderate performance overhead (2x-5x slowdown)
# - UndefinedBehaviorSanitizer has minimal performance overhead
#
# Build Presets:
# The project includes CMake presets for each sanitizer:
# - asan: AddressSanitizer build (Linux only)
# - ubsan: UndefinedBehaviorSanitizer build (Linux only)
# - tsan: ThreadSanitizer build (Linux only)
#
# Running with Sanitizers:
#   cmake --preset asan
#   cmake --build --preset asan
#   ctest --preset asan
# =============================================================================

# =============================================================================
# Function: enable_sanitizer
# =============================================================================
# Enables a specific sanitizer for a target
#
# Parameters:
#   target     - The CMake target to apply sanitizer flags to
#   sanitizer  - The type of sanitizer: "address", "undefined", or "thread"
#
# Supported Sanitizers:
#
# "address" - AddressSanitizer (ASan):
#   Detects memory errors including leaks, buffer overflows, use-after-free
#   Compiler flags: -fsanitize=address -fno-omit-frame-pointer -g
#   Linker flags: -fsanitize=address
#   Performance overhead: 2x-5x slowdown
#   Memory overhead: 2x-3x memory usage
#   Best for: Finding memory corruption bugs in testing
#
# "undefined" - UndefinedBehaviorSanitizer (UBSan):
#   Detects undefined behavior in C++ code
#   Compiler flags: -fsanitize=undefined -fno-sanitize-recover=all -g
#   Linker flags: -fsanitize=undefined
#   Performance overhead: Minimal (<2x slowdown)
#   Memory overhead: Minimal
#   Best for: Finding subtle UB bugs that may cause crashes
#
# "thread" - ThreadSanitizer (TSan):
#   Detects data races and threading issues
#   Compiler flags: -fsanitize=thread -g
#   Linker flags: -fsanitize=thread
#   Performance overhead: Significant (10x-20x slowdown)
#   Memory overhead: 5x-10x memory usage
#   Best for: Finding concurrency bugs in multi-threaded code
#
# Platform Support:
# - Linux (GCC/Clang): Full support for all sanitizers
# - macOS (Clang): Full support for all sanitizers
# - Windows (MSVC): Limited support, disabled by default
# - Windows (MinGW): Very limited support, disabled by default
#
# Note: Sanitizers have limited support on MSVC/MinGW. A warning is issued
# if used on Windows, and the function returns early without applying flags.
# =============================================================================
function(enable_sanitizer target sanitizer)
    # If no sanitizer specified, return early
    if(NOT sanitizer)
        return()
    endif()

    # Sanitizers have limited support on MSVC/MinGW
    # On Windows, static analysis tools like Visual Studio's /analyze may be used instead
    if(MSVC OR MINGW)
        message(WARNING "Sanitizers are not fully supported on MSVC/MinGW. "
                       "Use Visual Studio's /analyze flag or static analysis tools instead.")
        return()
    endif()

    # =========================================================================
    # AddressSanitizer (ASan) Configuration
    # =========================================================================
    # -fsanitize=address: Enable AddressSanitizer for memory error detection
    # -fno-omit-frame-pointer: Keep frame pointers for better stack traces in error reports
    # -g: Include debug information for accurate source line reporting
    #
    # ASan detects:
    # - Use-after-free: Accessing memory after it has been deallocated
    # - Heap buffer overflow: Reading/writing past allocated heap memory
    # - Stack buffer overflow: Corrupting the stack with out-of-bounds access
    # - Global buffer overflow: Accessing past global variable bounds
    # - Use-after-return: Accessing stack memory after function returns
    # - Use-after-scope: Accessing variables after they go out of scope
    # - Double free: Freeing the same memory pointer twice
    # - Memory leaks: Memory allocated but never freed
    # - Invalid free: Freeing memory that was not allocated
    # - Aligned misaddress: Accessing misaligned memory (on some architectures)
    #
    # Performance Impact:
    # - 2x-5x slowdown in execution time
    # - 2x-3x increase in memory usage
    # - Larger binary size (instruments all memory accesses)
    #
    # Best Practices:
    # - Run ASan on all tests before committing code
    # - Use with debug builds for best error reporting
    # - Combine with unit tests for maximum coverage
    # - May produce false positives with custom allocators
    # =========================================================================
    if("${sanitizer}" STREQUAL "address")
        target_compile_options(${target} PRIVATE
            -fsanitize=address
            -fno-omit-frame-pointer
            -g
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address
        )
    
    # =========================================================================
    # UndefinedBehaviorSanitizer (UBSan) Configuration
    # =========================================================================
    # -fsanitize=undefined: Enable UB detection for undefined behavior
    # -fno-sanitize-recover=all: Treat all UB as fatal errors (don't continue execution)
    # -g: Include debug information for accurate source line reporting
    #
    # UBSan detects:
    # - Signed integer overflow: Overflow in signed integer arithmetic (undefined in C++)
    # - Misaligned pointer access: Accessing memory with incorrect alignment
    # - Null pointer dereference: Dereferencing a null pointer
    # - Shift count out of range: Shifting by >= bit width of type
    # - Invalid enum values: Using values not in enum definition
    # - Floating point cast overflow: Converting out-of-range float to int
    # - Division by zero: Integer division by zero
    # - Object size violation: Passing too-small object to function
    # - Non-null return: Function declared non-null returns null
    # - Vptr access: Invalid virtual function table access
    # - Float cast overflow: Overflow in float-to-integer conversion
    #
    # Performance Impact:
    # - Minimal slowdown (<2x)
    # - Minimal memory overhead
    # - Small binary size increase
    #
    # Best Practices:
    # - Run UBSan on all tests to catch subtle UB bugs
    # - Combine with ASan for comprehensive checking
    # - Some checks may be noisy (e.g., signed overflow in hashing)
    # - Can be selective with -fsanitize=undefined behavior flags
    # =========================================================================
    elseif("${sanitizer}" STREQUAL "undefined")
        target_compile_options(${target} PRIVATE
            -fsanitize=undefined
            -fno-sanitize-recover=all
            -g
        )
        target_link_options(${target} PRIVATE
            -fsanitize=undefined
        )
    
    # =========================================================================
    # ThreadSanitizer (TSan) Configuration
    # =========================================================================
    # -fsanitize=thread: Enable thread sanitizer for data race detection
    # -g: Include debug information for accurate source line reporting
    #
    # TSan detects:
    # - Data races: Concurrent unsynchronized access to shared memory
    # - Lock order inversion: Potential deadlock due to inconsistent lock ordering
    # - Thread leaks: Threads created but never joined before exit
    # - Signal handler races: Unsafe operations in signal handlers
    # - Mutex double unlock: Unlocking a mutex that is not locked
    # - Destroying locked mutex: Destroying a mutex while still locked
    # - Thread creation from signal handler: Creating threads in signal handlers
    #
    # Performance Impact:
    # - Significant slowdown (10x-20x)
    # - Large memory overhead (5x-10x)
    # - Significant binary size increase
    #
    # Best Practices:
    # - Run TSan on multi-threaded tests only
    # - Not suitable for single-threaded code (no benefit, high overhead)
    # - May produce false positives with atomic operations
    # - Use with deadlock detection tools for comprehensive thread analysis
    # - Requires significant test execution time
    #
    # Limitations:
    # - Cannot detect deadlocks (use Helgrind or ThreadSanitizer's deadlock detector)
    # - High false positive rate with benign races
    # - May miss races that occur very rarely (timing-dependent)
    # =========================================================================
    elseif("${sanitizer}" STREQUAL "thread")
        target_compile_options(${target} PRIVATE
            -fsanitize=thread
            -g
        )
        target_link_options(${target} PRIVATE
            -fsanitize=thread
        )
    
    # Unknown sanitizer - warn the user
    else()
        message(WARNING "Unknown sanitizer: ${sanitizer}")
    endif()
endfunction()

# =============================================================================
# Function: enable_all_sanitizers
# =============================================================================
# Enables multiple sanitizers simultaneously for comprehensive checking
# Currently enables AddressSanitizer and UndefinedBehaviorSanitizer together
#
# Parameters:
#   target - The CMake target to apply sanitizer flags to
#
# Sanitizers Enabled:
# - AddressSanitizer (ASan): Memory error detection
# - UndefinedBehaviorSanitizer (UBSan): Undefined behavior detection
#
# Why not ThreadSanitizer?
# - ThreadSanitizer is not included by default because:
#   1. It has significant performance overhead (10x-20x slowdown)
#   2. It may conflict with other sanitizers in some configurations
#   3. It is only useful for multi-threaded code
#   4. It produces many false positives in complex threading scenarios
# - Enable ThreadSanitizer separately with enable_sanitizer(target "thread")
#
# Platform Support:
# - Linux (GCC/Clang): Full support for combined sanitizers
# - macOS (Clang): Full support for combined sanitizers
# - Windows (MSVC/MinGW): Limited support, disabled by default
#
# Best Practices:
# - Use enable_all_sanitizers for comprehensive checking in CI/CD
# - Run tests with combined sanitizers before every commit
# - May be too slow for local development on large codebases
# - Consider running individual sanitizers for faster iteration
# =============================================================================
function(enable_all_sanitizers target)
    # Sanitizers have limited support on MSVC/MinGW
    # On Windows, static analysis tools like Visual Studio's /analyze may be used instead
    if(MSVC OR MINGW)
        message(WARNING "Sanitizers are not fully supported on MSVC/MinGW. "
                       "Use Visual Studio's /analyze flag or static analysis tools instead.")
        return()
    endif()

    # =========================================================================
    # Combined Sanitizer Configuration
    # =========================================================================
    # Enables both AddressSanitizer and UndefinedBehaviorSanitizer together
    # This combination catches:
    # - Memory errors (buffer overflows, use-after-free, memory leaks)
    # - Undefined behavior (integer overflow, null dereference, misaligned access)
    #
    # Compiler Flags:
    # -fsanitize=address,undefined: Enable both ASan and UBSan
    # -fno-sanitize-recover=all: Treat all detected issues as fatal errors
    # -fno-omit-frame-pointer: Keep frame pointers for better stack traces
    # -g: Include debug information for accurate error reporting
    #
    # Performance Impact:
    # - 2x-5x slowdown (similar to ASan alone)
    # - 2x-3x memory overhead (similar to ASan alone)
    # - UBSan adds minimal overhead on top of ASan
    #
    # Best Practices:
    # - Use in CI/CD pipelines for comprehensive checking
    # - Run on all tests before merging code
    # - May be too slow for frequent local development
    # - Consider selective sanitizers for faster iteration
    # =========================================================================
    
    # Enable both AddressSanitizer and UndefinedBehaviorSanitizer
    # This combination catches a wide range of memory and UB issues
    target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined
        -fno-sanitize-recover=all
        -fno-omit-frame-pointer
        -g
    )
    target_link_options(${target} PRIVATE
        -fsanitize=address,undefined
    )
endfunction()
