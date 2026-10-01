# Contributing

Aevrix is a small educational server. Keep changes focused, explain why in the
commit, and prove them with a test or a command.

## Workflow

1. Branch from `main`, one topic per branch.
2. Build and test:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

3. Add `-DENABLE_TLS=OFF` once if you touched TLS-gated code, and run
   `bash tests/smoke/run_all.sh` when runtime behaviour changes.
4. Run the matching sanitizer: `cmake --preset asan|ubsan|tsan`, then build and
   test that preset.
5. Update `CHANGELOG.md` under `[Unreleased]`, the docs you affect and
   `aevrix.conf`, then open a PR. CI runs Debug, Release, TLS-off and all three
   sanitizer jobs.

## Style

``.clang-format` holds the rules (Google base, 4 spaces, 100 columns); run
`clang-format -i` yourself, CI does not check formatting.

Warnings are errors: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow
-Wold-style-cast -Wcast-align -Wunused` on GCC and Clang, `/W4 /WX` on MSVC.
Everything lives in `namespace aevrix` (`aevrix::http`, `aevrix::ws` for protocol code) behind `#pragma once`.
Types and methods are `PascalCase`, functions and variables `snake_case`, private members
carry a trailing underscore.

Ownership is explicit: `UniqueFd` for descriptors, RAII types for TLS and buffers.
Exceptions carry configuration, certificate and filesystem failures, never across the loop.

## Tests

New behaviour gets a suite in `tests/*_tests.cpp` registered with `add_test()` in
`CMakeLists.txt`; a bug fix gets a case that failed before it, and a security change
needs a negative test. Gaps: [docs/TESTING.md](docs/TESTING.md).

## Reviews

Keep PRs small and single-purpose, use conventional commit prefixes (`feat:`, `fix:`,
`docs:`, `test:`, `ci:`, `chore:`, `refactor:`, `perf:`) and never commit secrets.
Bugs use the issue templates; suspected vulnerabilities go through [SECURITY.md](SECURITY.md),
never a public issue. Participation follows [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
