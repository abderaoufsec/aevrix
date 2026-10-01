# Smoke tests

Live checks: each script starts `build/aevrix` (plus a Python mock upstream for
the proxy ones), exercises it, prints the results and tears everything down.

## Requirements

- A built server at `../../build/aevrix` (from the repo root:
  `cmake -S . -B build && cmake --build build -j`)
- `python3` and `curl` on PATH, plus `openssl` for the `wss://` leg of
  `smoke_ws.sh`

## Scripts

| Script | Covers |
|---|---|
| `smoke1.sh` | prefix stripping, `X-Forwarded-*`, chunked re-framing, status passthrough, HEAD, static files, keep-alive |
| `smoke2.sh` | 504 on upstream stall, HEAD timing, socket state |
| `smoke3.sh` | HEAD on static and proxied paths, GET after HEAD |
| `smoke4.sh` | asserted edge cases: oversize response, 1xx interim, 101 rejection, CL/TE smuggling rejection, truncated and close-delimited bodies, HEAD bodylessness, sequential keep-alive |
| `smoke_ws.sh` | WebSocket handshake, echo with 7-, 16- and 64-bit lengths, fragmentation, ping/pong, close, unmasked/RSV/oversize rejection, bad key/version/Connection/origin, plain-GET passthrough, idle timeout, static regression, plus `wss://` |
| `smoke_reload.sh` | SIGHUP hot swap, restart-only warning, invalid-config rejection, token-protected admin routes (401/400/404), SIGTERM shutdown |

`smoke4.sh`, `smoke_ws.sh` and `smoke_reload.sh` print
`RESULT: N passed, M failed` and exit non-zero on failure; `smoke1.sh` to
`smoke3.sh` are diagnostic and always exit 0.

## Running

```bash
./run_all.sh                 # all six in sequence
./smoke_ws.sh                # one script
SMOKE_OUT=/tmp/x ./smoke1.sh # custom scratch directory
```

Scratch files land in `$SMOKE_OUT` (default `/tmp/aevrix-smoke`). The servers
listen on `127.0.0.1:18080` and `:18081`, the mock upstreams on 19000-19006,
and the reload script on `:18192`; scripts run sequentially, so ports are
reused safely.
