# Coding Style and Linting Policy

## Code Formatting

Aevrix uses `clang-format` for consistent code formatting. The configuration is defined in `.clang-format` at the project root.

### Running the formatter

Before committing code, run:

```bash
# Format all files
clang-format -i src/*.cpp include/aevrix/*.h tests/*.cpp

# Or format specific files
clang-format -i src/main.cpp
```

### CI Integration

The CI pipeline checks that code is properly formatted. To format your code before pushing:

```bash
# Check if formatting is needed
clang-format --dry-run --Werror src/*.cpp include/aevrix/*.h tests/*.cpp
```

## Code Style Guidelines

### General Principles

- **RAII**: Use RAII for resource management (file descriptors, sockets, memory)
- **Explicit ownership**: Make ownership clear through types and naming
- **Small functions**: Keep functions focused and under 50 lines when possible
- **Clear naming**: Use descriptive names; avoid abbreviations unless well-known
- **Const correctness**: Use `const` liberally for variables that don't change
- **No exceptions**: Prefer error codes and explicit error handling for systems code

### Naming Conventions

- **Classes/Structs**: `PascalCase` (e.g., `ConnectionManager`, `HttpRequest`)
- **Functions/Methods**: `snake_case` (e.g., `parse_request`, `handle_connection`)
- **Variables**: `snake_case` (e.g., `socket_fd`, `buffer_size`)
- **Constants**: `kPascalCase` or `ALL_CAPS` (e.g., `kMaxConnections`, `MAX_HEADER_SIZE`)
- **Private members**: trailing underscore (e.g., `socket_fd_`, `buffer_`)

### File Organization

- **Headers**: `include/aevrix/` for public headers
- **Implementation**: `src/` for source files
- **Tests**: `tests/` for test files
- **Documentation**: `docs/` for documentation

### Include Guards

Use `#pragma once` for header guards:

```cpp
#pragma once

namespace aevrix {
// ...
}
```

### Namespace

All code should be in the `aevrix` namespace:

```cpp
namespace aevrix {

class TcpListener {
    // ...
};

} // namespace aevrix
```

## Compiler Warnings

The project is built with strict warning flags:
- GCC/Clang: `-Wall -Wextra -Wpedantic`
- MSVC: `/W4`

**Treat all warnings as errors**. The CI pipeline will fail if there are any warnings.

## Static Analysis

### clang-tidy

Run `clang-tidy` for additional static analysis:

```bash
# Run on all files
clang-tidy src/*.cpp include/aevrix/*.h -- -std=c++20

# Run on specific file
clang-tidy src/main.cpp -- -std=c++20
```

### Recommended clang-tidy checks

- `modernize-*`: Modern C++ features
- `performance-*`: Performance improvements
- `bugprone-*`: Common bugs
- `cppcoreguidelines-*`: C++ Core Guidelines

## Testing Style

### Unit Tests

- Test names should be descriptive: `TEST(Parser, ValidGetRequest)`
- One test per logical case
- Use AAA pattern: Arrange, Act, Assert
- Test both success and failure paths

### Integration Tests

- Test end-to-end functionality
- Use realistic scenarios
- Clean up resources after each test

## Commit Message Style

Follow conventional commits:

```
feat: add TCP listener implementation
fix: resolve memory leak in connection handler
test: add parser edge case tests
docs: update architecture documentation
refactor: simplify connection state machine
```

## Review Checklist

Before submitting code for review:

- [ ] Code is formatted with `clang-format`
- [ ] No compiler warnings
- [ ] No `clang-tidy` warnings
- [ ] Tests pass
- [ ] Sanitizers pass (ASan, UBSan)
- [ ] Documentation updated if needed
- [ ] Commit message follows conventional commits
- [ ] No debug code or commented-out code
- [ ] RAII used for all resources
- [ ] Error handling is explicit

## IDE Configuration

### VS Code

Install these extensions:
- C/C++ (Microsoft)
- C/C++ Extension Pack (Microsoft)
- clang-format (xaver)

Configure settings.json:
```json
{
  "C_Cpp.default.cppStandard": "c++20",
  "C_Cpp.default.compilerPath": "/usr/bin/g++",
  "editor.formatOnSave": true,
  "C_Cpp.formatting": "clangFormat"
}
```

### CLion

Settings → Editor → Code Style → C/C++ → Set from → .clang-format

## Windows Development Notes

When developing on Windows:
- Use WSL2 for Linux compatibility testing
- The primary target is Linux/POSIX
- Windows support may be added later
- Ensure code compiles with both GCC/Clang and MSVC when possible

### Prerequisites for Windows Development

1. Install CMake: https://cmake.org/download/
2. Install a C++ compiler:
   - Visual Studio Community (includes MSVC)
   - MinGW-w64 for GCC on Windows
   - Or use WSL2 for full Linux environment

### Building on Windows

```bash
# Using Visual Studio generator
cmake -B build -G "Visual Studio 17 2022"
cmake --build build

# Using MinGW
cmake -B build -G "MinGW Makefiles"
cmake --build build
```
