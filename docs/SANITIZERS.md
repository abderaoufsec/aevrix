# Aevrix Sanitizers and Static Analysis Documentation

## Overview

This document describes the sanitizers and static analysis tools used in the Aevrix project to find bugs that ordinary tests miss. Sanitizers are dynamic analysis tools that instrument code at compile time to detect various classes of bugs at runtime.

## Sanitizers

### AddressSanitizer (ASan)

**Purpose**: Detects memory errors including leaks, buffer overflows, and use-after-free bugs.

**What it detects**:
- Use-after-free: Accessing memory after it has been deallocated
- Heap buffer overflow: Reading/writing past allocated heap memory
- Stack buffer overflow: Corrupting the stack with out-of-bounds access
- Global buffer overflow: Accessing past global variable bounds
- Use-after-return: Accessing stack memory after function returns
- Use-after-scope: Accessing variables after they go out of scope
- Double free: Freeing the same memory pointer twice
- Memory leaks: Memory allocated but never freed
- Invalid free: Freeing memory that was not allocated
- Aligned misaddress: Accessing misaligned memory (on some architectures)

**Performance Impact**:
- 2x-5x slowdown in execution time
- 2x-3x increase in memory usage
- Larger binary size (instruments all memory accesses)

**Usage**:
```bash
# Configure with AddressSanitizer
cmake --preset asan

# Build
cmake --build --preset asan

# Run tests
ctest --preset asan
```

**Best Practices**:
- Run ASan on all tests before committing code
- Use with debug builds for best error reporting
- Combine with unit tests for maximum coverage
- May produce false positives with custom allocators

### UndefinedBehaviorSanitizer (UBSan)

**Purpose**: Detects undefined behavior in C++ code.

**What it detects**:
- Signed integer overflow: Overflow in signed integer arithmetic (undefined in C++)
- Misaligned pointer access: Accessing memory with incorrect alignment
- Null pointer dereference: Dereferencing a null pointer
- Shift count out of range: Shifting by >= bit width of type
- Invalid enum values: Using values not in enum definition
- Floating point cast overflow: Converting out-of-range float to int
- Division by zero: Integer division by zero
- Object size violation: Passing too-small object to function
- Non-null return: Function declared non-null returns null
- Vptr access: Invalid virtual function table access
- Float cast overflow: Overflow in float-to-integer conversion

**Performance Impact**:
- Minimal slowdown (<2x)
- Minimal memory overhead
- Small binary size increase

**Usage**:
```bash
# Configure with UndefinedBehaviorSanitizer
cmake --preset ubsan

# Build
cmake --build --preset ubsan

# Run tests
ctest --preset ubsan
```

**Best Practices**:
- Run UBSan on all tests to catch subtle UB bugs
- Combine with ASan for comprehensive checking
- Some checks may be noisy (e.g., signed overflow in hashing)
- Can be selective with -fsanitize=undefined behavior flags

### ThreadSanitizer (TSan)

**Purpose**: Detects data races and threading issues in multi-threaded code.

**What it detects**:
- Data races: Concurrent unsynchronized access to shared memory
- Lock order inversion: Potential deadlock due to inconsistent lock ordering
- Thread leaks: Threads created but never joined before exit
- Signal handler races: Unsafe operations in signal handlers
- Mutex double unlock: Unlocking a mutex that is not locked
- Destroying locked mutex: Destroying a mutex while still locked
- Thread creation from signal handler: Creating threads in signal handlers

**Performance Impact**:
- Significant slowdown (10x-20x)
- Large memory overhead (5x-10x)
- Significant binary size increase

**Usage**:
```bash
# Configure with ThreadSanitizer
cmake --preset tsan

# Build
cmake --build --preset tsan

# Run tests
ctest --preset tsan
```

**Best Practices**:
- Run TSan on multi-threaded tests only
- Not suitable for single-threaded code (no benefit, high overhead)
- May produce false positives with atomic operations
- Use with deadlock detection tools for comprehensive thread analysis
- Requires significant test execution time

**Limitations**:
- Cannot detect deadlocks (use Helgrind or ThreadSanitizer's deadlock detector)
- High false positive rate with benign races
- May miss races that occur very rarely (timing-dependent)

## Static Analysis

### Compiler Warnings

The project uses strict compiler warnings treated as errors to enforce code quality:

**GCC/Clang (Linux/Unix)**:
- `-Wall`: Enable all warnings about questionable code construction
- `-Wextra`: Enable extra warnings not covered by -Wall
- `-Wpedantic`: Warn about non-standard C++ constructs (ISO C++ compliance)
- `-Werror`: Treat all warnings as errors (fail build on any warning)
- `-Wconversion`: Warn about implicit type conversions that may change values
- `-Wsign-conversion`: Warn about implicit sign conversions
- `-Wshadow`: Warn when a variable shadows another variable
- `-Wold-style-cast`: Warn about C-style casts (prefer C++ casts)
- `-Wcast-align`: Warn about pointer casts that increase alignment requirements
- `-Wunused`: Warn about unused variables, functions, labels
- `-Woverloaded-virtual`: Warn about overloaded virtual functions
- `-Wnon-virtual-dtor`: Warn about non-virtual destructors in polymorphic classes

**MSVC (Windows)**:
- `/W4`: Enable high warning level (includes warnings about suspicious code)
- `/WX`: Treat warnings as errors (fail build on any warning)
- `/permissive-`: Disable non-standard language extensions (enforce standard compliance)
- `/Zc:__cplusplus`: Enable correct __cplusplus macro value

## Platform Limitations

### Linux (GCC/Clang)
- Full support for all sanitizers (ASan, UBSan, TSan)
- Full support for all compiler warnings
- Recommended for development and CI/CD

### macOS (Clang)
- Full support for all sanitizers (ASan, UBSan, TSan)
- Full support for all compiler warnings
- Recommended for development and CI/CD

### Windows (MSVC)
- Limited sanitizer support (ASan partially supported in newer versions)
- Use Visual Studio's `/analyze` flag for static analysis instead
- Full support for compiler warnings
- Development builds may not have full sanitizer coverage

### Windows (MinGW)
- Very limited sanitizer support
- Sanitizers disabled by default due to lack of support
- Full support for compiler warnings
- Consider using WSL or Linux VM for full sanitizer coverage

## CMake Presets

The project includes CMake presets for each sanitizer configuration:

| Preset | Sanitizer | Platform | Build Directory |
|--------|-----------|----------|----------------|
| `asan` | AddressSanitizer | Linux/macOS | `build/asan` |
| `ubsan` | UndefinedBehaviorSanitizer | Linux/macOS | `build/ubsan` |
| `tsan` | ThreadSanitizer | Linux/macOS | `build/tsan` |

## Using Sanitizers in Development

### Local Development Workflow

1. **Regular Development** (fast builds):
   ```bash
   cmake --preset debug-win  # or debug on Linux
   cmake --build --preset debug-win
   ```

2. **Pre-commit Check** (comprehensive checking):
   ```bash
   # On Linux/macOS
   cmake --preset asan
   cmake --build --preset asan
   ctest --preset asan
   ```

3. **Multi-threaded Code Testing**:
   ```bash
   # On Linux/macOS
   cmake --preset tsan
   cmake --build --preset tsan
   ctest --preset tsan
   ```

### CI/CD Integration

For CI/CD pipelines, use the following order:

1. **Build with warnings as errors** (catch obvious issues)
2. **Run unit tests** (verify functionality)
3. **Run with ASan** (catch memory errors)
4. **Run with UBSan** (catch undefined behavior)
5. **Run with TSan** (catch threading issues, if applicable)

Example CI configuration:
```yaml
test:
  script:
    - cmake --preset debug
    - cmake --build --preset debug
    - ctest --preset debug
    - cmake --preset asan
    - cmake --build --preset asan
    - ctest --preset asan
    - cmake --preset ubsan
    - cmake --build --preset ubsan
    - ctest --preset ubsan
```

## Interpreting Sanitizer Reports

### AddressSanitizer Report Format

```
==12345==ERROR: AddressSanitizer: heap-use-after-free on address 0x12345678
    #0 0x12345678 in function_name file.cpp:10:5
    #1 0x23456789 in another_function file.cpp:20:10
    #2 0x34567890 in main main.cpp:30:5

0x12345678 is located 0 bytes inside of 16-byte region [0x12345678,0x12345688)
freed by thread T0 here:
    #0 0x45678901 in free libc.so
    #1 0x56789012 in deallocate memory.cpp:15:5

previously allocated by thread T0 here:
    #0 0x67890123 in malloc libc.so
    #1 0x78901234 in allocate memory.cpp:10:5
```

**Key Information**:
- Error type (heap-use-after-free)
- Memory address involved
- Stack trace of where the error occurred
- Stack trace of where memory was freed
- Stack trace of where memory was allocated

### UndefinedBehaviorSanitizer Report Format

```
file.cpp:10:5: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
    #0 0x12345678 in function_name file.cpp:10:5
```

**Key Information**:
- File and line where UB occurred
- Type of undefined behavior
- Explanation of why it's undefined
- Stack trace

### ThreadSanitizer Report Format

```
WARNING: ThreadSanitizer: data race (Write/Read)
  Write (thread T1):
    #0 0x12345678 in write_variable file.cpp:10:5
    #1 0x23456789 in thread_function thread.cpp:20:10

  Previous read (thread T0):
    #0 0x34567890 in read_variable file.cpp:15:5
    #1 0x45678901 in main main.cpp:30:5

  Location is global 'g_counter' of size 4 at 0x12345678
```

**Key Information**:
- Type of race (Write/Read, Write/Write, Read/Write)
- Threads involved
- Stack traces for both accesses
- Location of shared variable

## Common Issues and Solutions

### False Positives

Sanitizers may produce false positives in certain situations:

**ASan False Positives**:
- Custom memory allocators not recognized by ASan
- Interop with libraries that have their own memory management
- False positives with signal handlers

**UBSan False Positives**:
- Signed integer overflow in hash functions (may be intentional)
- Strict alignment checks on architectures that support unaligned access
- False positives with bit manipulation code

**TSan False Positives**:
- Benign races in lock-free algorithms
- False positives with atomic operations
- Races in performance-critical code that are intentional

**Solutions**:
- Use `__attribute__((no_sanitize("address")))` to suppress specific ASan warnings
- Use `__attribute__((no_sanitize("undefined")))` to suppress specific UBSan warnings
- Use `__attribute__((no_sanitize("thread")))` to suppress specific TSan warnings
- Only suppress when you have verified the code is correct
- Document why suppression is necessary

### Performance Issues

If sanitizers make development too slow:

1. **Run sanitizers less frequently**: Only run before commits or in CI
2. **Use selective sanitizers**: Only run ASan for memory-focused development
3. **Reduce test scope**: Run a subset of tests with sanitizers
4. **Use release builds with sanitizers**: `-O1` instead of `-O0` for faster execution

### Memory Exhaustion

Sanitizers increase memory usage significantly:

1. **Increase swap space**: Ensure sufficient swap for sanitizer runs
2. **Reduce parallel test execution**: Run tests sequentially instead of in parallel
3. **Use machine with more RAM**: Run sanitizer tests on a machine with 16GB+ RAM

## Resources

- [AddressSanitizer Documentation](https://github.com/google/sanitizers/wiki/AddressSanitizer)
- [UndefinedBehaviorSanitizer Documentation](https://github.com/google/sanitizers/wiki/UndefinedBehaviorSanitizer)
- [ThreadSanitizer Documentation](https://github.com/google/sanitizers/wiki/ThreadSanitizer)
- [GCC Warning Options](https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html)
- [Clang Warning Options](https://clang.llvm.org/docs/DiagnosticsReference.html)
