# Changelog

All notable changes to Aevrix, in the format of
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), following
[semantic versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- GoogleTest is now provided by CMake: `find_package(GTest)` first, then a
  pinned FetchContent build of googletest v1.15.2. The TLS suites build on
  runners without `libgtest-dev`. Escape hatches: `-DAEVRIX_FETCH_GTEST=OFF`,
  `-DAEVRIX_USE_SYSTEM_GTEST=OFF`.
- Release builds define `NDEBUG` only for the server: test targets get
  `-UNDEBUG`, so `<cassert>` checks still run under `-O3` and the Release build
  no longer fails on assert-only variables under `-Werror`.

### Changed
- Documentation rewritten against the code: added `docs/HTTP.md` and
  `docs/CONFIGURATION.md`; `docs/ARCHITECTURE.md`, `docs/DEPLOYMENT.md`,
  `docs/OBSERVABILITY.md`, `docs/TESTING.md`, `docs/BENCHMARKS.md`, `README.md`,
  `SECURITY.md` and `CONTRIBUTING.md` shortened and corrected; TLS, sanitizer,
  coding-style and HTTP-correctness notes folded into the files above.
- CI runs `build-and-test` (Debug and Release presets), a TLS-off build and an
  ASan/UBSan/TSan matrix; the release workflow installs the `openssl` CLI.
- Issue templates tightened; a bug template that named a `--log-level` flag the
  server does not have was replaced with the real flags and keys; blank issues
  are disabled via `config.yml`.

## [1.0.0] - 2026-09-30

### Added
- Event-driven HTTP/1.1 core on epoll: incremental request parser, connection
  and write state machines, keep-alive, bounded worker pool for filesystem
  work, graceful shutdown on SIGINT/SIGTERM.
- Static file serving with MIME detection, document-root confinement and
  traversal protection.
- TLS through OpenSSL on its own listener, with a `tls_*` configuration set and
  unit and integration suites.
- Reverse proxy to `http://` upstreams with connection pooling, timeouts,
  `X-Forwarded-*`, regenerated framing and refusal of ambiguous upstream
  responses.
- RFC 6455 WebSocket upgrade: handshake validation, path and origin allowlists,
  frame parsing, fragmentation, Ping/Pong/Close handling, size limits.
- Atomic configuration reload through `ServerConfigStore`: SIGHUP, the
  opt-in admin routes, and a hot-versus-restart classification per key.
- Structured logging with connection and request identifiers, a log-level
  setting, and a validated `ServerConfig` with a sample `aevrix.conf`.
- Test surface: 20 CTest suites (17 without TLS), six live smoke scripts and
  sanitizer presets.

### Notes
- `GET /metrics` is a placeholder, the metrics collector is not wired into the
  server, and `/server-info` is not registered. ETag, Last-Modified, Range and
  conditional requests exist as tested library helpers but are not applied to
  static responses. Request framing is more permissive than RFC 9112 allows;
  `docs/HTTP.md` lists the deviations.

[Unreleased]: https://github.com/abderaoufsec/aevrix/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/abderaoufsec/aevrix/releases/tag/v1.0.0
