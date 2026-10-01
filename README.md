# Aevrix

Aevrix is an HTTP/1.1 server written from scratch in C++20: one non-blocking
epoll loop, a bounded worker pool for filesystem work, and small components
that follow the RFCs they implement.

Version 1.0.0, educational. It has no security audit, fuzzing or load testing
behind it and is not hardened for untrusted Internet traffic. Read the
maturity statement and the known limitations in [SECURITY.md](SECURITY.md)
before deploying it anywhere that matters. Linux is the platform it is
developed and tested on.

## Build and run

```bash
sudo apt install cmake g++ libssl-dev      # omit libssl-dev for a TLS-less build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/aevrix --config aevrix.conf        # serves ./public on 127.0.0.1:8080
curl http://127.0.0.1:8080/health
```

`--root PATH` overrides the configured document root, `--help` lists the
flags, and the CMake presets `debug`, `release`, `asan`, `ubsan` and `tsan`
build the same tree with Ninja.

## Endpoints

| Path | Behaviour | Auth |
|---|---|---|
| `GET /` | 200 `text/plain`, body `Aevrix HTTP Server v1.0.0` | none |
| `GET /health` | 200 `text/plain`, body `OK` | none |
| `GET /metrics` | 200 placeholder text; no metrics are collected yet | none |
| `GET /admin/config` | 200 generation and reload counters, 404 while `admin_api_enabled = false` | `admin_token` when set |
| `POST /admin/reload-config` | 200 or 400 reload result, 404 while disabled | `admin_token` when set |
| anything else | static file from `document_root`, else 404 or 403 | none |

Routes match on method and the complete target, so a query string does not
reach one. HEAD handling, framing and the deviations this server makes from
RFC 9110, 9111 and 9112 are in [docs/HTTP.md](docs/HTTP.md).

## Configuration

One flat `key = value` file, passed with `--config`; without it the built-in
defaults apply (127.0.0.1, port 8080, `./public`, four workers). Every key,
its default and whether it reloads or needs a restart:
[docs/CONFIGURATION.md](docs/CONFIGURATION.md). The commented sample is
[aevrix.conf](aevrix.conf).

## Tests

```bash
ctest --test-dir build --output-on-failure     # 20 suites with TLS, 17 without
bash tests/smoke/run_all.sh                    # live server, mock upstream, WebSocket
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
```

The CTest suites cover the parser, router, connections, timeouts, path
security, proxy framing and pooling, WebSocket frames and handshake, TLS,
config reload and the cache helpers; the smoke scripts drive a real server
end to end. What is missing is listed in [docs/TESTING.md](docs/TESTING.md).

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): components, threading, dispatch, reload
- [docs/HTTP.md](docs/HTTP.md): methods, framing, caching, RFC deviations
- [docs/CONFIGURATION.md](docs/CONFIGURATION.md): every configuration key
- [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md): requirements, TLS, systemd, troubleshooting
- [docs/OBSERVABILITY.md](docs/OBSERVABILITY.md): endpoints, logs, metrics
- [docs/TESTING.md](docs/TESTING.md): suites, smoke tests, sanitizers
- [docs/BENCHMARKS.md](docs/BENCHMARKS.md): harness, how to run it
- [tests/smoke/README.md](tests/smoke/README.md): what each smoke script covers
- [SECURITY.md](SECURITY.md), [CONTRIBUTING.md](CONTRIBUTING.md),
  [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md), [CHANGELOG.md](CHANGELOG.md)

## License

MIT, see [LICENSE](LICENSE).
