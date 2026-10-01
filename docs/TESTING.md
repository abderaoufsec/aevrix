# Testing

CTest suites link components directly, smoke scripts drive a running server,
and sanitizer builds cover both.

## CTest suites

`cmake` registers 20 suites (`ctest --test-dir build`), 17 without TLS: the
three TLS suites need OpenSSL and GoogleTest, and `path_security_tests` is
UNIX-only.

| Area | Suites |
|---|---|
| HTTP core | `http_tests`, `http_request_tests`, `http_cache_tests`, `router_tests` |
| Connections | `connection_nonblocking_tests`, `connection_manager_tests`, `output_state_machine_tests`, `timeout_enforcement_tests` |
| Security | `path_security_tests` (UNIX only) |
| Proxy | `proxy_target_tests`, `proxy_request_builder_tests`, `proxy_response_parser_tests`, `upstream_pool_tests` (not Windows) |
| WebSocket | `websocket_frame_tests`, `websocket_handshake_tests` |
| TLS | `tls_context_tests`, `tls_connection_tests`, `tls_integration_tests` |
| Observability, configuration | `metrics_tests`, `config_reload_tests` |

Most suites assert with `<cassert>`, and `CMakeLists.txt` undefines `NDEBUG`
for test targets, so a Release run checks as much as a Debug run. GoogleTest
is resolved automatically (`find_package`, then a pinned FetchContent build).

```bash
cmake --preset debug   && cmake --build --preset debug   && ctest --preset debug --output-on-failure
cmake --preset release && cmake --build --preset release && ctest --preset release --output-on-failure
cmake -S . -B build-notls -DENABLE_TLS=OFF -DCMAKE_BUILD_TYPE=Debug && cmake --build build-notls && ctest --test-dir build-notls
```

## Smoke scripts

`bash tests/smoke/run_all.sh` starts real servers plus a Python mock upstream
and exercises the proxy, WebSocket, HEAD, re-framing, smuggling rejection,
SIGHUP reload, admin auth and clean shutdown. `smoke4.sh`, `smoke_ws.sh` and
`smoke_reload.sh` exit non-zero on failure. All need `build/aevrix`, `curl` and
`python3`; per-script coverage is in
[tests/smoke/README.md](../tests/smoke/README.md).

## Sanitizers

Run the one that matches the change:

```bash
cmake --preset asan  && cmake --build --preset asan  && ASAN_OPTIONS=detect_leaks=1      ctest --preset asan
cmake --preset ubsan && cmake --build --preset ubsan && UBSAN_OPTIONS=print_stacktrace=1 ctest --preset ubsan
cmake --preset tsan  && cmake --build --preset tsan  && TSAN_OPTIONS=halt_on_error=1     ctest --preset tsan
```


## Not covered

- Pipelined requests: the second request in a segment is never served and no
  test asserts it (see [HTTP.md](HTTP.md)).
- Wire-level caching and ranges: `http_cache_tests` covers the helpers while
  the server sends no ETag and never a 206; `metrics_tests` covers a collector
  that nothing updates.
- Protocol conformance, fuzzing, load and soak testing: none exist.
- Windows: no CI runner builds that path.
