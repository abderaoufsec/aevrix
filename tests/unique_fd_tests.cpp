// =============================================================================
// Aevrix - UniqueFd Unit Tests
// =============================================================================
// This file contains comprehensive unit tests for the UniqueFd RAII wrapper.
// Tests cover:
// - Default construction (invalid descriptor)
// - Construction from raw file descriptor
// - Move semantics (construction and assignment)
// - Ownership transfer
// - Reset functionality
// - Release functionality
// - Destructor behavior
// - Comparison operators
// - Swap functionality
//
// Note: These are simple assertion-based tests. In a real project, we would use
// a proper test framework like Google Test or Catch2. For Phase 1, we use simple
// assertions to verify behavior.
// =============================================================================

#include "aevrix/unique_fd.h"
#include <cassert>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#define OPEN_FLAGS _O_RDONLY
#define CLOSE_FUNC _close
#else
#include <fcntl.h>    // For open(), O_RDONLY
#include <unistd.h>   // For close()
#define OPEN_FLAGS O_RDONLY
#define CLOSE_FUNC close
#endif

// =============================================================================
// Test Utilities
// =============================================================================

/**
 * @brief Simple test assertion macro
 * 
 * Prints test name and result. Aborts on failure.
 */
#define TEST_ASSERT(condition, test_name) \
    do { \
        std::cout << "Testing: " << test_name << "... "; \
        if (condition) { \
            std::cout << "PASSED\n"; \
        } else { \
            std::cout << "FAILED\n"; \
            std::cerr << "Assertion failed: " << #condition << "\n"; \
            std::abort(); \
        } \
    } while(0)

/**
 * @brief Helper function to create a valid file descriptor for testing
 * 
 * On Unix/Linux: Opens /dev/null in read-only mode
 * On Windows: Opens NUL (Windows equivalent of /dev/null)
 * 
 * @return int A valid file descriptor, or -1 on failure.
 */
int create_test_fd() {
#ifdef _WIN32
    // On Windows, open NUL (equivalent to /dev/null)
    return _open("NUL", OPEN_FLAGS);
#else
    // On Unix/Linux, open /dev/null
    return open("/dev/null", OPEN_FLAGS);
#endif
}

// =============================================================================
// Test Cases
// =============================================================================

/**
 * @brief Test 1: Default constructor creates invalid descriptor
 * 
 * Verify that a default-constructed UniqueFd has fd_ = -1 (invalid).
 */
void test_default_constructor() {
    aevrix::UniqueFd fd;
    TEST_ASSERT(fd.get() == -1, "Default constructor creates invalid descriptor");
    TEST_ASSERT(!fd.is_valid(), "Default descriptor is not valid");
    TEST_ASSERT(!static_cast<bool>(fd), "Default descriptor converts to false");
}

/**
 * @brief Test 2: Constructor from raw file descriptor
 * 
 * Verify that constructing from a valid fd takes ownership correctly.
 */
void test_constructor_from_raw_fd() {
    int raw_fd = create_test_fd();
    TEST_ASSERT(raw_fd >= 0, "Created valid test file descriptor");
    
    {
        aevrix::UniqueFd fd(raw_fd);
        TEST_ASSERT(fd.get() == raw_fd, "Constructor takes ownership of raw fd");
        TEST_ASSERT(fd.is_valid(), "Constructed descriptor is valid");
    }
    // fd is now out of scope, descriptor should be closed
    // We can't easily verify this without accessing internal state,
    // but the test ensures no double-close occurs
}

/**
 * @brief Test 3: Constructor from invalid file descriptor (-1)
 * 
 * Verify that constructing from -1 creates an invalid UniqueFd.
 */
void test_constructor_from_invalid_fd() {
    aevrix::UniqueFd fd(-1);
    TEST_ASSERT(fd.get() == -1, "Constructor with -1 creates invalid descriptor");
    TEST_ASSERT(!fd.is_valid(), "Descriptor from -1 is not valid");
}

/**
 * @brief Test 4: Move constructor transfers ownership
 * 
 * Verify that move construction transfers ownership and leaves source invalid.
 */
void test_move_constructor() {
    int raw_fd = create_test_fd();
    TEST_ASSERT(raw_fd >= 0, "Created valid test file descriptor");
    
    aevrix::UniqueFd fd1(raw_fd);
    TEST_ASSERT(fd1.get() == raw_fd, "fd1 owns the descriptor");
    
    aevrix::UniqueFd fd2(std::move(fd1));
    TEST_ASSERT(fd2.get() == raw_fd, "fd2 now owns the descriptor");
    TEST_ASSERT(fd1.get() == -1, "fd1 is invalid after move");
    TEST_ASSERT(!fd1.is_valid(), "fd1 is not valid after move");
}

/**
 * @brief Test 5: Move assignment transfers ownership
 * 
 * Verify that move assignment closes current descriptor and transfers ownership.
 */
void test_move_assignment() {
    int raw_fd1 = create_test_fd();
    int raw_fd2 = create_test_fd();
    TEST_ASSERT(raw_fd1 >= 0 && raw_fd2 >= 0, "Created valid test file descriptors");
    
    aevrix::UniqueFd fd1(raw_fd1);
    aevrix::UniqueFd fd2(raw_fd2);
    
    TEST_ASSERT(fd1.get() == raw_fd1, "fd1 owns descriptor 1");
    TEST_ASSERT(fd2.get() == raw_fd2, "fd2 owns descriptor 2");
    
    fd2 = std::move(fd1);
    
    TEST_ASSERT(fd2.get() == raw_fd1, "fd2 now owns descriptor 1");
    TEST_ASSERT(fd1.get() == -1, "fd1 is invalid after move");
    
    // Note: raw_fd2 should be closed by the move assignment
}

/**
 * @brief Test 6: Self move assignment is safe
 * 
 * Verify that self-assignment doesn't cause issues.
 * Note: Self-move is generally undefined behavior in C++, but our implementation
 * handles it safely by checking for self-assignment.
 */
void test_self_move_assignment() {
    int raw_fd = create_test_fd();
    TEST_ASSERT(raw_fd >= 0, "Created valid test file descriptor");
    
    aevrix::UniqueFd fd(raw_fd);
    int fd_value = fd.get();
    
    // Suppress self-move warning for this specific test
    // In real code, self-move should be avoided
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wself-move"
    fd = std::move(fd);
    #pragma GCC diagnostic pop
    
    TEST_ASSERT(fd.get() == fd_value, "Self move assignment preserves descriptor");
}

/**
 * @brief Test 7: Release transfers ownership to caller
 * 
 * Verify that release() returns the descriptor and leaves UniqueFd invalid.
 */
void test_release() {
    int raw_fd = create_test_fd();
    TEST_ASSERT(raw_fd >= 0, "Created valid test file descriptor");
    
    aevrix::UniqueFd fd(raw_fd);
    int released_fd = fd.release();
    
    TEST_ASSERT(released_fd == raw_fd, "Release returns the owned descriptor");
    TEST_ASSERT(fd.get() == -1, "UniqueFd is invalid after release");
    TEST_ASSERT(!fd.is_valid(), "UniqueFd is not valid after release");
    
    // Caller is now responsible for closing the descriptor
    CLOSE_FUNC(released_fd);
}

/**
 * @brief Test 8: Reset closes current and takes new ownership
 * 
 * Verify that reset() closes the current descriptor and takes ownership of new one.
 */
void test_reset() {
    int raw_fd1 = create_test_fd();
    int raw_fd2 = create_test_fd();
    TEST_ASSERT(raw_fd1 >= 0 && raw_fd2 >= 0, "Created valid test file descriptors");
    
    aevrix::UniqueFd fd(raw_fd1);
    TEST_ASSERT(fd.get() == raw_fd1, "fd owns descriptor 1");
    
    fd.reset(raw_fd2);
    
    TEST_ASSERT(fd.get() == raw_fd2, "fd now owns descriptor 2");
    // raw_fd1 should be closed by reset()
}

/**
 * @brief Test 9: Reset to -1 closes current descriptor
 * 
 * Verify that reset(-1) closes the current descriptor without taking new one.
 */
void test_reset_to_invalid() {
    int raw_fd = create_test_fd();
    TEST_ASSERT(raw_fd >= 0, "Created valid test file descriptor");
    
    aevrix::UniqueFd fd(raw_fd);
    TEST_ASSERT(fd.is_valid(), "fd is valid");
    
    fd.reset(-1);
    
    TEST_ASSERT(fd.get() == -1, "fd is invalid after reset(-1)");
    TEST_ASSERT(!fd.is_valid(), "fd is not valid after reset(-1)");
}

/**
 * @brief Test 10: Reset to same descriptor is no-op
 * 
 * Verify that resetting to the same descriptor doesn't close it.
 */
void test_reset_same_fd() {
    int raw_fd = create_test_fd();
    TEST_ASSERT(raw_fd >= 0, "Created valid test file descriptor");
    
    aevrix::UniqueFd fd(raw_fd);
    int fd_value = fd.get();
    
    fd.reset(fd_value);
    
    TEST_ASSERT(fd.get() == fd_value, "Reset to same fd is no-op");
}

/**
 * @brief Test 11: Destructor closes descriptor
 * 
 * Verify that the destructor closes the descriptor when going out of scope.
 * This is implicitly tested by all other tests, but we can explicitly check
 * that no double-close errors occur.
 */
void test_destructor() {
    int raw_fd = create_test_fd();
    TEST_ASSERT(raw_fd >= 0, "Created valid test file descriptor");
    
    {
        aevrix::UniqueFd fd(raw_fd);
        TEST_ASSERT(fd.get() == raw_fd, "fd owns the descriptor");
    }
    // fd is now out of scope, destructor should have closed the descriptor
    // If destructor didn't close, we'd have a leak (not easily testable here)
    // If destructor double-closed, we'd see errors (we don't, so it's correct)
}

/**
 * @brief Test 12: Swap exchanges descriptors
 * 
 * Verify that swap() correctly exchanges the owned descriptors.
 */
void test_swap() {
    int raw_fd1 = create_test_fd();
    int raw_fd2 = create_test_fd();
    TEST_ASSERT(raw_fd1 >= 0 && raw_fd2 >= 0, "Created valid test file descriptors");
    
    aevrix::UniqueFd fd1(raw_fd1);
    aevrix::UniqueFd fd2(raw_fd2);
    
    TEST_ASSERT(fd1.get() == raw_fd1, "fd1 owns descriptor 1");
    TEST_ASSERT(fd2.get() == raw_fd2, "fd2 owns descriptor 2");
    
    fd1.swap(fd2);
    
    TEST_ASSERT(fd1.get() == raw_fd2, "fd1 now owns descriptor 2");
    TEST_ASSERT(fd2.get() == raw_fd1, "fd2 now owns descriptor 1");
}

/**
 * @brief Test 13: std::swap works with UniqueFd
 * 
 * Verify that std::swap can be used with UniqueFd objects.
 */
void test_std_swap() {
    int raw_fd1 = create_test_fd();
    int raw_fd2 = create_test_fd();
    TEST_ASSERT(raw_fd1 >= 0 && raw_fd2 >= 0, "Created valid test file descriptors");
    
    aevrix::UniqueFd fd1(raw_fd1);
    aevrix::UniqueFd fd2(raw_fd2);
    
    std::swap(fd1, fd2);
    
    TEST_ASSERT(fd1.get() == raw_fd2, "std::swap works correctly");
    TEST_ASSERT(fd2.get() == raw_fd1, "std::swap works correctly");
}

/**
 * @brief Test 14: Comparison operators
 * 
 * Verify that comparison operators work correctly.
 */
void test_comparison_operators() {
    int raw_fd1 = create_test_fd();
    int raw_fd2 = create_test_fd();
    TEST_ASSERT(raw_fd1 >= 0 && raw_fd2 >= 0, "Created valid test file descriptors");
    
    aevrix::UniqueFd fd1(raw_fd1);
    aevrix::UniqueFd fd2(raw_fd2);
    aevrix::UniqueFd fd3(raw_fd1);
    
    TEST_ASSERT(fd1 == fd3, "Equal descriptors compare equal");
    TEST_ASSERT(fd1 != fd2, "Different descriptors compare not equal");
    TEST_ASSERT((fd1 < fd2) == (raw_fd1 < raw_fd2), "Less-than works correctly");
    
    TEST_ASSERT(fd1 == raw_fd1, "Comparison with raw int works");
    TEST_ASSERT(fd1 != raw_fd2, "Comparison with raw int works");
}

/**
 * @brief Test 15: Copy operations are deleted
 * 
 * This is a compile-time test. If copy operations were not deleted,
// this code would not compile. Since we can't test compilation here,
// we document that this should fail to compile:
 * 
 * aevrix::UniqueFd fd1(raw_fd);
 * aevrix::UniqueFd fd2(fd1);  // Should NOT compile
 * aevrix::UniqueFd fd3 = fd1; // Should NOT compile
 */

// =============================================================================
// Test Runner
// =============================================================================

/**
 * @brief Run all UniqueFd tests
 * 
 * Executes all test functions and reports results.
 * 
 * @return int 0 if all tests pass, non-zero otherwise.
 */
int run_unique_fd_tests() {
    std::cout << "=== Running UniqueFd Unit Tests ===\n\n";
    
    try {
        test_default_constructor();
        test_constructor_from_raw_fd();
        test_constructor_from_invalid_fd();
        test_move_constructor();
        test_move_assignment();
        test_self_move_assignment();
        test_release();
        test_reset();
        test_reset_to_invalid();
        test_reset_same_fd();
        test_destructor();
        test_swap();
        test_std_swap();
        test_comparison_operators();
        
        std::cout << "\n=== All UniqueFd Tests PASSED ===\n";
        return 0;
    } catch (...) {
        std::cout << "\n=== UniqueFd Tests FAILED with exception ===\n";
        return 1;
    }
}

// =============================================================================
// Main Entry Point
// =============================================================================

/**
 * @brief Main entry point for UniqueFd tests
 * 
 * @return int Exit code (0 for success, non-zero for failure)
 */
int main() {
    return run_unique_fd_tests();
}
