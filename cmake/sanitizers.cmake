# =============================================================================
# Aevrix - Sanitizer Configuration Module
# =============================================================================
# This CMake module provides functions to enable various memory and thread
# sanitizers for the Aevrix project. Sanitizers help detect:
# - Memory leaks (AddressSanitizer)
# - Undefined behavior (UndefinedBehaviorSanitizer)
# - Data races (ThreadSanitizer)
#
# Usage:
#   include(cmake/sanitizers.cmake)
#   enable_sanitizer(target_name "address")
#   enable_all_sanitizers(target_name)
# =============================================================================

# =============================================================================
# Function: enable_sanitizer
# =============================================================================
# Enables a specific sanitizer for a target
#
# Parameters:
#   target  - The CMake target to apply sanitizer flags to
#   sanitizer - The type of sanitizer: "address", "undefined", or "thread"
#
# Supported sanitizers:
#   - "address": AddressSanitizer - detects memory leaks, buffer overflows, use-after-free
#   - "undefined": UndefinedBehaviorSanitizer - detects undefined behavior
#   - "thread": ThreadSanitizer - detects data races and threading issues
#
# Note: Sanitizers have limited support on MSVC. A warning is issued if used on Windows.
# =============================================================================
function(enable_sanitizer target sanitizer)
    # If no sanitizer specified, return early
    if(NOT sanitizer)
        return()
    endif()

    # Sanitizers have limited support on MSVC
    if(MSVC)
        message(WARNING "Sanitizers are not fully supported on MSVC")
        return()
    endif()

    # AddressSanitizer configuration
    # -fsanitize=address: Enable AddressSanitizer
    # -fno-omit-frame-pointer: Keep frame pointers for better stack traces
    # -g: Include debug information for accurate error reporting
    if("${sanitizer}" STREQUAL "address")
        target_compile_options(${target} PRIVATE
            -fsanitize=address
            -fno-omit-frame-pointer
            -g
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address
        )
    
    # UndefinedBehaviorSanitizer configuration
    # -fsanitize=undefined: Enable UB detection
    # -fno-sanitize-recover=all: Don't recover from UB (treat as fatal)
    # -g: Include debug information
    elseif("${sanitizer}" STREQUAL "undefined")
        target_compile_options(${target} PRIVATE
            -fsanitize=undefined
            -fno-sanitize-recover=all
            -g
        )
        target_link_options(${target} PRIVATE
            -fsanitize=undefined
        )
    
    # ThreadSanitizer configuration
    # -fsanitize=thread: Enable thread sanitizer for data race detection
    # -g: Include debug information
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
# Note: ThreadSanitizer is not included by default as it can have significant
# performance overhead and may conflict with other sanitizers.
# =============================================================================
function(enable_all_sanitizers target)
    # Sanitizers have limited support on MSVC
    if(MSVC)
        message(WARNING "Sanitizers are not fully supported on MSVC")
        return()
    endif()

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
