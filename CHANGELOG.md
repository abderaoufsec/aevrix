# Changelog

All notable changes to Aevrix are documented in this file. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-09-30

Stable v1.0.0 release. Definition of Done verified: 20/20 CTest (TLS-ON),
17/17 (TLS-OFF), ASan/UBSan/TSan clean, 6/6 smoke scripts (proxy, WebSocket,
reload). Release engineering: SECURITY.md, CONTRIBUTING.md, CHANGELOG.md,
CODE_OF_CONDUCT.md, DEPLOYMENT.md, GitHub issue/PR templates, release
workflow. Project version bumped to 1.0.0 (CMake, `Server: Aevrix/1.0.0`,
`/server-info`). TSan race fixed in upstream-pool test listener.

First stable release: a complete, tested HTTP/1.1 server in C++20.

### Added
- Event-driven HTTP/1.1 core: non-blocking epoll loop, explicit connection
  state machine, incremental request parser, response/output state machine,
  keep-alive, bounded worker pool for filesystem work.
- Static file serving with MIME detection, path normalization, traversal
  protection, conditional requests (ETag, Last-Modified, If-None-Match,
  If-Modified-Since), Range requests, HTTP cache.
- Observability: structured logging (connection/request IDs, levels), metrics
  collection, `GET /metrics` (Prometheus text), `GET /health`,
  `GET /server-info`.
- Validated `ServerConfig` + `aevrix.conf` sample (`host`, `port`, `workers`,
  timeouts, limits, TLS, proxy, WebSocket, admin, log level).
- TLS/HTTPS via OpenSSL (Phase 21): non-blocking handshake/read/write,
  TLS 1.2 minimum, `tls_*` config, unit + integration tests,
  `docs/TLS.md` (`-DENABLE_TLS=ON/OFF`).
- Reverse proxy (Phase 22): request forwarding, connection pooling
  (`UpstreamPool`), timeouts, failure mapping (502/504), X-Forwarded-*
  headers, chunked re-framing, smuggling rejection, `proxy_*` config, smoke
  harness (`mock_upstream.py`, `proxy.conf`, `smoke1-4.sh`).
- WebSocket upgrade RFC 6455 (Phase 23): handshake validation (key/accept,
  version 13, Connection token, path/origin allowlists), framing
  (FIN/RSV/opcode/mask, 7/16/64-bit lengths), fragmentation reassembly,
  Ping/Pong/Close echo policy, UTF-8 + size limits, `wss://` via TLS,
  timeouts, `websocket_*` config, `ws.conf` + `ws_client.py` + `smoke_ws.sh`.
- Atomic configuration reload (Phase 24): copy-on-write `ServerConfigStore`
  (atomic `shared_ptr` swap, snapshot generations), SIGHUP via signal-safe
  flag + eventfd wake-up, token-protected `GET /admin/config` (token masked)
  and `POST /admin/reload-config` (same-directory `?path=` guard), hot vs
  restart-required classification with logging, `admin_api_enabled` /
  `admin_token` config, unit + smoke coverage (`smoke_reload.sh`).
- Timeouts and resource limits: header/body/keep-alive/write timeouts,
  `max_connections`, `max_buffer_size`, `max_request_body`, graceful
  SIGINT/SIGTERM shutdown.
- Test pyramid: 20 CTest suites (parser, router, cache, metrics, connections,
  timeouts, path security, proxy, upstream pool, WebSocket, TLS, config
  reload) plus live smoke scripts (`tests/smoke/run_all.sh`).
- Benchmark harness (`benchmarks/`, `docs/BENCHMARKS.md`), sanitizer presets
  (ASan/UBSan/TSan, `docs/SANITIZERS.md`), `.clang-format` style config.

### Fixed
- Windows/MinGW build compatibility (sign-conversion scoping, presets).
- Double-encoded path traversal handled without exceptions; `validate_path()`
  returns proper HTTP status codes.
- TLS event-loop integration: WANT_READ/WANT_WRITE epoll interest, timeout
  budgets, shutdown Close handling.

### Security
- Documented in SECURITY.md: traversal protection, strict framing rejection
  (CL/TE conflicts, smuggling), size/time limits, TLS 1.2+ only, WebSocket
  masking + origin allowlist, token-protected admin surface, no secrets in
  logs, coordinated disclosure policy.

## History (pre-1.0 development)

Key milestones on the way to v1.0 (see `git log` for the full sequence):

- `v0.1.0 TCP + hard-coded HTTP` — TCP listener, RAII fd ownership
  (`docs/IMPLEMENTATION_ROADMAP.md`).
- Stages 1–7: non-blocking event-driven foundation, HTTP parser, response
  state machine, WorkerPool, timeout enforcement, static-file security audit.
- Phase 14: structured logging. Phase 15: graceful shutdown. Phase 16: test
  pyramid. Phase 19: HTTP correctness (ETag, cache, ranges). Phase 20:
  observability (metrics, `/metrics`, `/health`).
- Phase 21: TLS/HTTPS. Phase 22: reverse proxy. Phase 23: WebSocket upgrade.
  Phase 24: atomic configuration reload.

[Unreleased]: https://github.com/abderaoufsec/aevrix/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/abderaoufsec/aevrix/releases/tag/v1.0.0
