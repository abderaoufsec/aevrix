# Contributing to Aevrix

Thanks for your interest in Aevrix! This is an educational C++20 HTTP/1.1
server. Contributions are welcome — bug reports, documentation, tests, and
careful, well-scoped code changes.

## Ground Rules

- Educational project: clarity and correctness beat cleverness.
- **No new protocol features in patch releases** — keep changes focused.
- Security-sensitive code (parser, TLS, proxy framing, WebSocket, path
  validation, admin auth) needs extra care and tests.
- Be kind. See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## Development Workflow

1. Fork the repo and create a topic branch from `main`:
   `git checkout -b fix/describe-the-fix`.
2. Build and test locally (Linux-first; Windows/MinGW supported):
   ```bash
   cmake -S . -B build -DENABLE_TLS=ON -DCMAKE_BUILD_TYPE=Debug
   cmake --build build -j$(nproc)
   ctest --test-dir build --output-on-failure
   ```
   GoogleTest for the TLS suites is resolved automatically (`find_package`,
   then a pinned FetchContent fallback), so no extra package install is needed.
   Also build once with `-DENABLE_TLS=OFF` if you touched TLS-gated code.
3. Run the smoke suite when you touch runtime behaviour:
   `bash tests/smoke/run_all.sh` (needs `build/aevrix`).
4. Run at least one sanitizer preset for parser/network/memory changes:
   `cmake --preset asan|ubsan|tsan` → build → `ctest --preset <name>`
   (see `docs/SANITIZERS.md`; TSan is required for threading changes).
5. Format with `.clang-format` (Google base, 4-space, 100 cols) and keep
   `-Wall -Wextra -Wconversion` clean — warnings are errors.
6. Commit with conventional messages (`feat:`, `fix:`, `docs:`, `test:`,
   `chore:`, `perf:`, `refactor:`, `ci:`), then open a pull request.

## Code Style

- C++20, `aevrix::` / `aevrix::http::` namespaces, RAII ownership
  (`UniqueFd`, `TlsContext`/`TlsConnection`), no raw `new`/`delete` in new
  code, no exceptions across the event loop.
- Headers in `include/aevrix/`, implementation in `src/`, one component per
  file; Doxygen comments on public APIs.
- Event-loop thread owns sockets/TLS state; blocking work goes to
  `WorkerPool`; signal handlers do flag + eventfd write only.
- Config keys are `snake_case` in `aevrix.conf` / `ServerConfig`; document
  new keys in `aevrix.conf`, `docs/ARCHITECTURE.md`, and `TESTING.md` if
  behaviour is user-visible.

## Testing Requirements

- Every bug fix adds a regression test; every behaviour change updates or
  adds tests. New suites live in `tests/*_tests.cpp`, wired into
  `CMakeLists.txt` with `add_test()`, and documented in `docs/TESTING.md`.
- Target: `ctest` 20/20 green, smoke suite green, no sanitizer findings.
- Security-relevant changes (framing, paths, limits, auth, masking, origin
  checks) must include negative tests (malformed / adversarial inputs).

## Pull Request Process

1. Fill in `.github/pull_request_template.md` completely (description,
   testing, docs, breaking-change notice).
2. Keep PRs small and single-purpose; separate refactors from fixes.
3. CI must pass (Debug + Release, the TLS-OFF build, and the
   ASan/UBSan/TSan matrix). A maintainer reviews for correctness, style,
   tests, and docs.
4. Security fixes may be handled privately — see
   [SECURITY.md](SECURITY.md). **Never post a PoC exploit in a public PR.**

## Issue Reporting

- Bugs: use the bug template — version/commit, config (redacted), repro
  steps, expected vs actual, logs.
- Features: use the feature template — problem, proposed scope, why it fits
  an educational server. Large features need a design discussion first.
- Security: **do not file public issues** — follow `SECURITY.md`.

## Documentation

- User-visible changes update `README.md` and/or the relevant `docs/*.md`
  (`ARCHITECTURE`, `TESTING`, `TLS`, `DEPLOYMENT`, `BENCHMARKS`).
- Behavioural changes get a `CHANGELOG.md` entry under `[Unreleased]`.

## License

By contributing you agree your changes are released under the MIT License
(see [LICENSE](LICENSE)).
