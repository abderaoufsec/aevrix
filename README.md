# Aevrix

A small, fast, security-conscious HTTP/1.1 server written from scratch in C++20.

## Overview

Aevrix is a Linux-first HTTP/1.1 server designed around explicit ownership, non-blocking network I/O, and strict protocol parsing. The project aims to demonstrate how far a disciplined C++ developer can take a web server while keeping the architecture understandable.

## Status

**Status:** Design document — implementation not started

Aevrix is currently in the planning phase. See the [implementation roadmap](docs/IMPLEMENTATION_ROADMAP.md) for detailed phase information.

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
- TLS support
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
