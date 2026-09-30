# Aevrix

![CI](https://github.com/abderaoufsec/aevrix/actions/workflows/ci.yml/badge.svg)

A small, fast, security-conscious HTTP/1.1 server written from scratch in C++20.

**v1.0.0** — event-driven core, static files, TLS, reverse proxy, WebSocket, atomic config reload. Linux-first (epoll), educational by design: explicit ownership, strict parsing, bounded work, tested components.

## Features

- **HTTP/1.1 core** — incremental request parser (RFC 9112 framing), response/output state machine, keep-alive, HEAD, conditional requests (ETag, Last-Modified, If-None-Match, If-Modified-Since), Range requests.
- **Static files** — MIME detection, document-root confinement, traversal protection (`..`, encoded, absolute, Windows paths rejected).
- **Event-driven runtime** — single non-blocking epoll loop, connection state machine, bounded `WorkerPool` for filesystem work, `ConnectionManager` caps.
- **Timeouts + limits** — header/body/keep-alive/write timeouts; `max_connections`, `max_buffer_size`, `max_request_body`; graceful SIGINT/SIGTERM shutdown.
- **TLS/HTTPS** (OpenSSL) — non-blocking handshake/read/write, TLS 1.2 minimum (1.0/1.1, SSLv2/3 disabled), `tls_*` config; see [docs/TLS.md](docs/TLS.md).
- **Reverse proxy** — forwarding with pooled upstream connections, connect/read/idle timeouts, 502/504 mapping, `X-Forwarded-*` headers, chunked re-framing, smuggling rejection; `proxy_*` config.
- **WebSocket** (RFC 6455) — handshake validation (key/accept, version 13, Connection token, path/origin allowlists), masked-frame enforcement, fragmentation reassembly, Ping/Pong/Close policy, UTF-8 + size limits, `wss://` via TLS; `websocket_*` config.
- **Config reload** — copy-on-write `ServerConfigStore` (atomic snapshot swap, in-flight requests keep their generation), SIGHUP + token-protected `POST /admin/reload-config`, hot vs restart-required classification, all-or-nothing validation.
- **Observability** — structured logs (connection/request IDs, levels), metrics collection, `GET /metrics` (Prometheus text), `GET /health`, `GET /server-info`; opt-in `GET /admin/config`, `POST /admin/reload-config`.

## Quick Start

```bash
# 1. Install build deps (Debian/Kali)
sudo apt install cmake ninja-build g++ libssl-dev

# 2. Build
cmake -S . -B build -DENABLE_TLS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 3. Serve ./public on 127.0.0.1:8080
./build/aevrix --config aevrix.conf

# 4. Try it
curl http://127.0.0.1:8080/
curl http://127.0.0.1:8080/health
curl http://127.0.0.1:8080/metrics
```

Try the live subsystems with the smoke configs:

```bash
# Reverse proxy (needs mock upstream on :19000)
python3 tests/smoke/mock_upstream.py 19000 &
./build/aevrix -c tests/smoke/proxy.conf
curl http://127.0.0.1:18080/api/hello

# WebSocket echo
./build/aevrix -c tests/smoke/ws.conf
python3 tests/smoke/ws_client.py

# Config reload without restart
kill -HUP $(pidof aevrix)   # hot keys apply; restart-only keys warn in the log
```

## Building

| Command | Purpose |
|---|---|
| `cmake -S . -B build -DENABLE_TLS=ON -DCMAKE_BUILD_TYPE=Release` | standard Release build |
| `cmake --build build -j$(nproc)` | compile |
| `cmake -S . -B build-notls -DENABLE_TLS=OFF ...` | build without TLS (OpenSSL optional then) |
| `cmake --preset debug / release / asan / ubsan / tsan` | preset configs (Ninja; sanitizers in [docs/SANITIZERS.md](docs/SANITIZERS.md)) |

Requirements: CMake >= 3.20, C++20 compiler (GCC 11+), Threads; OpenSSL dev libs for TLS. Windows/MinGW builds work; production validation is Linux.

## Configuration

All keys live in `aevrix.conf` (sample shipped, validated by `ServerConfig`):

```ini
host = 127.0.0.1
port = 8080
workers = 4
document_root = ./public
header_timeout_ms = 10000
body_timeout_ms = 30000
keep_alive_timeout_ms = 5000
write_timeout_ms = 30000
max_connections = 1000
max_buffer_size = 65536
max_request_body = 10485760
tls_enabled = false
log_level = info
```

TLS, proxy, WebSocket, and admin keys are documented (commented) in the sample conf. Hot-reloadable via SIGHUP or `POST /admin/reload-config`: timeouts, limits, log level, proxy timeouts/pool, WebSocket budgets. Restart-required: `host`, `port`, `workers`, `document_root`, TLS identity, `proxy_pass`/`proxy_prefix`, `websocket_paths` — the log says so on reload. Key reference: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md); production guidance: [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md).

## Testing

```bash
ctest --test-dir build --output-on-failure   # 20 unit suites
bash tests/smoke/run_all.sh                  # 6 live smoke scripts (proxy, ws, reload)
```

Unit suites cover parser, router, cache, metrics, connections, timeouts, path security, proxy framing/pool, WebSocket frames/handshake, TLS context/connection/integration, config reload (TSan-clean). Live smoke scripts start a real server (+ Python mock upstream) and assert behaviour end-to-end. Details: [docs/TESTING.md](docs/TESTING.md); smoke catalogue: [tests/smoke/README.md](tests/smoke/README.md); benchmarks: [docs/BENCHMARKS.md](docs/BENCHMARKS.md).

## Deployment Notes

Run tagged releases as a non-root user, read-only `document_root`, `0600` on `aevrix.conf` + TLS key; front with a hardened proxy/firewall for Internet exposure. Prefer TLS with a real CA cert; disable the admin API unless needed (long random token, loopback bind, network firewall). Monitor `GET /health` + `GET /metrics` (Prometheus) and structured logs; canary + smoke-test before promoting. Full runbook: [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md); policy: [SECURITY.md](SECURITY.md).

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — system design, threading, data flow, invariants
- [docs/TESTING.md](docs/TESTING.md) — test pyramid, how to run, coverage
- [docs/TLS.md](docs/TLS.md) — TLS architecture, config, troubleshooting
- [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) — production runbook
- [docs/OBSERVABILITY.md](docs/OBSERVABILITY.md) — logs, metrics, endpoints
- [docs/HTTP_CORRECTNESS.md](docs/HTTP_CORRECTNESS.md) — framing, cache, ranges
- [docs/BENCHMARKS.md](docs/BENCHMARKS.md) — harness, methodology, baselines
- [docs/SANITIZERS.md](docs/SANITIZERS.md) — ASan/UBSan/TSan usage
- [docs/IMPLEMENTATION_ROADMAP.md](docs/IMPLEMENTATION_ROADMAP.md) — phase history

## Releases

Tagged `v*.*.*` releases build (Release, TLS ON), test, and publish via [.github/workflows/release.yml](.github/workflows/release.yml): `aevrix-linux-x86_64` binary, `aevrix.conf.example`, `BUILD-INFO.txt` (version, commit, compiler, build mode, platform), `SHA256SUMS.txt`, plus generated notes. Verify with `sha256sum -c SHA256SUMS.txt`. Breaking changes and migration notes: [CHANGELOG.md](CHANGELOG.md).

## Contributing

Issues, docs, tests, and focused PRs are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) (workflow, style, testing bar, templates in `.github/`). Security reports go through [SECURITY.md](SECURITY.md), never public issues. Community standards: [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## License

MIT License — see [LICENSE](LICENSE) for details.

## Acknowledgments

Informed by NGINX (event-driven workers), Caddy (core/module separation), Drogon (modern C++ non-blocking I/O), Crow, cpp-httplib, and RFC 9110 (Semantics) / RFC 9112 (HTTP/1.1) / RFC 6455 (WebSocket).


## Architecture

```
                    ┌──────────────────────┐
                    │       Clients        │
                    │ browsers / curl / LB │
                    └──────────┬───────────┘
                               │ TCP
                               ▼
                    ┌──────────────────────┐
                    │     TCP Listener     │
                    │ socket/bind/listen   │
                    └──────────┬───────────┘
                               │ accept
                               ▼
                    ┌──────────────────────┐
                    │     Event Loop       │
                    │        epoll         │
                    └──────────┬───────────┘
                               │ events
                               ▼
                    ┌──────────────────────┐
                    │ Connection Manager   │
                    └──────────┬───────────┘
                               │
                               ▼
                    ┌──────────────────────┐
                    │ Connection State     │
                    │ Machine              │
                    └──────────┬───────────┘
                               │ bytes
                               ▼
                    ┌──────────────────────┐
                    │ HTTP/1.1 Parser      │
                    └──────────┬───────────┘
                               │ Request
                               ▼
                    ┌──────────────────────┐
                    │ Router / Dispatcher  │
                    └──────────┬───────────┘
                               │
               ┌───────────────┴───────────────┐
               ▼                               ▼
      ┌─────────────────┐             ┌─────────────────┐
      │ Static File     │             │ Application      │
      │ Resolver        │             │ Handler          │
      └────────┬────────┘             └────────┬────────┘
               │                               │
               └───────────────┬───────────────┘
                               ▼
                    ┌──────────────────────┐
                    │ Response Builder     │
                    └──────────┬───────────┘
                               ▼
                              TCP
```

## Features

### Tier A — Required (Planned)
- TCP listener with IPv4 support
- HTTP/1.1 request parser and response serializer
- GET and HEAD methods
- Static file serving with MIME type detection
- Keep-alive connections
- Request size limits and timeouts
- Structured logging
- Graceful shutdown
- Comprehensive unit and integration tests

### Tier B — Core Differentiators (Planned)
- Linux non-blocking sockets with epoll
- Explicit connection state machine
- Bounded worker pool for blocking filesystem work
- Configurable limits and atomic configuration reload
- Access logging and metrics endpoint
- Security test suite

### Tier C — Advanced (Future)
- Routing with parameters
- Reverse proxy
- Gzip/brotli compression
- WebSocket upgrade
- HTTP range and conditional requests

## Quick Start

*Note: This section will be updated once the implementation reaches a usable state.*

Planned usage:
```bash
# Build the project
cmake --preset debug
cmake --build --preset debug

# Run the server
./aevrix --config aevrix.toml

# Or with command-line options
./aevrix --root ./public --port 8080
```

## Documentation

- [Master Strategy](docs/MASTER_STRATEGY.md) — Project vision and engineering principles
- [Architecture](docs/ARCHITECTURE.md) — Detailed system architecture and design decisions
- [Implementation Roadmap](docs/IMPLEMENTATION_ROADMAP.md) — Phase-by-phase implementation plan

## Building

```bash
# Configure and build in debug mode
cmake --preset debug
cmake --build --preset debug

# Configure and build in release mode
cmake --preset release
cmake --build --preset release

# Run tests
ctest --preset debug
```

### TLS/HTTPS Support

Aevrix supports TLS/HTTPS using OpenSSL. To enable TLS:

**On Debian/Kali:**
```bash
sudo apt install libssl-dev
cmake -S . -B build -DENABLE_TLS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

**Configuration:**
Add TLS settings to your configuration file:
```
tls_enabled = true
tls_cert_file = /path/to/cert.pem
tls_key_file = /path/to/key.pem
tls_min_version = TLSv1.2
tls_max_version = TLSv1.3
tls_port = 8443
```

**Generating a self-signed certificate for testing:**
```bash
openssl genrsa -out key.pem 2048
openssl req -new -x509 -key key.pem -out cert.pem -days 365 -subj "/CN=localhost"
```

**Testing with curl:**
```bash
curl -k https://localhost:8443/
```

The `-k` flag bypasses certificate verification for self-signed certificates.

## Testing

Aevrix includes:
- Unit tests for individual components
- Integration tests for end-to-end functionality
- Security tests for protocol edge cases
- Protocol torture tests for malformed input

## Benchmarks

*Note: Benchmarks will be added as the implementation progresses. All benchmark results will include complete methodology documentation.*

## Security Model

Aevrix follows several security principles:
- Protocol correctness before performance
- Strict input validation and size limits
- Protection against directory traversal attacks
- Resource exhaustion prevention
- Every protocol bug becomes a regression test

See the [architecture document](docs/ARCHITECTURE.md) for detailed security considerations.

## Contributing

*Note: Contributing guidelines will be added as the project matures.*

## License

MIT License — see [LICENSE](LICENSE) for details.

## Acknowledgments

Aevrix is informed by several real implementations and standards:
- NGINX (master/worker process model, event-driven processing)
- Caddy (clean separation of core and modules)
- Drogon (modern C++ non-blocking I/O)
- Crow (approachable C++ HTTP routing)
- cpp-httplib (compact C++ HTTP implementation)
- RFC 9110 (HTTP Semantics)
- RFC 9112 (HTTP/1.1)

## Disclaimer

Aevrix is a learning project. Do not claim "production-ready" until the project has earned that label through testing, security review, interoperability testing, and sustained use.
