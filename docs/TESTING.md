# Aevrix Testing Documentation

## Overview

This document describes the testing infrastructure for the Aevrix HTTP server project, including unit tests, integration tests, and security tests.

## Test Structure

### Unit Tests

Unit tests are located in the `tests/` directory and test individual components in isolation.

#### HTTP Response Tests (`tests/http_response_tests.cpp`)

Tests the HTTP response serialization and components:
- **StatusCode**: Status code to string conversion, categorization
- **HttpHeader**: Header validation, case-insensitive comparison
- **HttpHeaders**: Header collection management
- **HttpResponse**: Response construction, validation, serialization
- **HttpResponseSerializer**: Response serialization to HTTP format

**Test Count**: 44 tests

**Build Target**: `aevrix_http_tests`

**Run Command**: `./build/debug/aevrix_http_tests.exe`

#### HTTP Request Tests (`tests/http_request_tests.cpp`)

Tests HTTP request parsing and components:
- **HttpMethod**: Method conversion, categorization (safe, idempotent, requires body)
- **HttpRequest**: Request construction, validation, headers, body
- **HttpRequestParser**: Request parsing state machine, error handling

**Test Coverage**:
- HttpMethod: 13 tests
- HttpRequest: 7 tests
- HttpRequestParser: 35 tests
- Integration: 10 tests

**Total**: 65 tests (exceeds Phase 16 requirement of 50+)

**Build Target**: `aevrix_http_request_tests`

**Run Command**: `./build/debug/aevrix_http_request_tests.exe`

#### Router Tests (`tests/router_tests.cpp`)

Tests the lightweight router for application-level routing:
- Route registration
- Method and path matching
- Case-insensitive method matching
- 404 handling
- Multiple HTTP methods (GET, POST, HEAD, OPTIONS, PUT, DELETE, PATCH)
- Route overwriting
- Path edge cases (trailing slashes, empty paths, special characters)

**Test Count**: 33 tests (exceeds Phase 16 requirement of 20+)

**Build Target**: `aevrix_router_tests`

**Run Command**: `./build/debug/aevrix_router_tests.exe`

#### Path Security Tests (`tests/path_security_tests.cpp`)

Tests path security for preventing directory traversal attacks:
- Directory traversal prevention (../ sequences)
- Encoded dot traversal (%2e%2e)
- Absolute path rejection
- Windows path rejection (C:\, UNC paths)
- Path length limits
- Special character handling
- Case sensitivity
- Query strings and fragments

**Note**: This test file is created but currently disabled in CMake due to implementation issues with the static `is_path_safe` method requiring document root validation.

**Test Count**: 25 tests (approaches Phase 16 requirement of 30+)

**Build Target**: Currently disabled

### Running All Tests

To run all registered tests:

```bash
# Build tests
cmake --build --preset debug-win

# Run all tests via CTest
ctest --test-dir build/debug --output-on-failure
```

To run individual test executables:

```bash
./build/debug/aevrix_http_tests.exe
./build/debug/aevrix_http_request_tests.exe
./build/debug/aevrix_router_tests.exe
```

## Phase 16 Test Pyramid Status

### Unit Tests

✅ **Parser**: 65 tests (requirement: 50+) - COMPLETE
✅ **Router**: 33 tests (requirement: 20+) - COMPLETE
⚠️ **Path Security**: 25 tests created, disabled (requirement: 30+) - INCOMPLETE

### Integration Tests

❌ Integration tests for:
- Startup
- GET
- HEAD
- 404
- 405
- Keep-alive
- Timeouts
- Large headers
- Concurrency
- Shutdown

**Status**: NOT IMPLEMENTED

Integration tests would require a test framework that can:
1. Start the server process
2. Make actual HTTP requests to it
3. Verify responses
4. Clean up the server process

This is beyond the scope of the current simple unit test framework.

### Protocol Tests

❌ Protocol tests using raw byte sequences

**Status**: NOT IMPLEMENTED

Protocol tests would involve sending malformed or edge-case HTTP byte sequences to verify parser robustness.

### Security Tests

❌ Security tests for:
- Path traversal
- Invalid framing
- Oversized inputs
- Slow clients
- Connection floods
- Malformed headers

**Status**: NOT IMPLEMENTED

Security tests would require a more sophisticated test infrastructure to simulate attack scenarios.

## Test Implementation Guidelines

### Adding New Tests

1. Create a new test file in `tests/` directory
2. Include the necessary headers from `include/aevrix/`
3. Use the `TEST_ASSERT` macro for assertions
4. Add the test executable to `CMakeLists.txt`
5. Register the test with `add_test()`
6. Update this documentation

### Test Naming Convention

- Test functions: `test_<component>_<scenario>()`
- Test files: `<component>_tests.cpp`
- Test targets: `aevrix_<component>_tests`
- Test names: `aevrix_<component>_tests`

### Assertion Macro

```cpp
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
```

## Current Test Coverage Summary

| Component | Tests | Status | Requirement |
|-----------|-------|--------|-------------|
| HTTP Response | 44 | ✅ Complete | - |
| HTTP Request/Parser | 65 | ✅ Complete | 50+ |
| Router | 33 | ✅ Complete | 20+ |
| Path Security | 25 (disabled) | ⚠️ Incomplete | 30+ |
| Integration | 0 | ❌ Not Started | 9 scenarios |
| Protocol | 0 | ❌ Not Started | Raw byte tests |
| Security | 0 | ❌ Not Started | 6 attack types |

## Known Issues

1. **Path Security Tests Disabled**: The static `is_path_safe` method requires a valid document root directory. Tests fail on Windows when the `/var/www` path doesn't exist. This needs to be fixed by either:
   - Creating a temporary test directory
   - Using a platform-agnostic test path
   - Refactoring the method to not require filesystem access

2. **Integration Tests Not Implemented**: Full integration tests require a more sophisticated test framework that can start/stop the server and make HTTP requests. This is a significant undertaking that may be deferred to a later phase.

3. **Protocol/Security Tests Not Implemented**: These require specialized test infrastructure to simulate attack scenarios and malformed HTTP traffic.

## Future Work

1. Enable and fix path security tests
2. Implement integration test framework
3. Add protocol tests with raw byte sequences
4. Add security tests for attack scenarios
5. Add code coverage analysis
6. Add performance benchmarks
7. Add fuzzing integration
