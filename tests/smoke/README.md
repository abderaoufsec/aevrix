# Smoke tests (live end-to-end)

Live end-to-end checks. Each script starts a local `aevrix` server (plus a
Python mock upstream for the proxy scripts), exercises it, prints the
results, and tears everything down.

## Prerequisites

- Server binary built at `../../build/aevrix` (from the repo root:
  `cmake -S . -B build && cmake --build build -j`)
- `python3` and `curl` on PATH
- `openssl` on PATH (only for the `wss://` leg of `smoke_ws.sh`)

## Scripts

| Script | Covers |
|---|---|
| `smoke1.sh` | prefix stripping, X-Forwarded-* headers, chunked re-framing, status passthrough, HEAD, static files, keep-alive |
| `smoke2.sh` | 504 on upstream stall, HEAD timing, socket state |
| `smoke3.sh` | HEAD on static vs proxied paths, GET after HEAD |
| `smoke4.sh` | asserted edge cases: oversize response (`max_response_bytes`), 1xx interim, 101 upgrade rejection, CL/TE smuggling rejection, truncated body, close-delimited body, HEAD bodylessness (success + error replies), sequential keep-alive |
| `smoke_ws.sh` | Phase 23 WebSocket: `ws.conf` + raw-socket `ws_client.py` — handshake, text/binary echo (7-bit, 16-bit, 64-bit lengths), fragmentation, ping/pong, close handshake, unmasked/RSV/oversize rejection, bad key/version/Connection/origin (400/426/403), plain-GET passthrough, idle timeout, static regression; plus `wss://` handshake/echo/close |

`smoke4.sh` and `smoke_ws.sh` print `RESULT: N passed, M failed` and exit
non-zero on any failure. `smoke1-3` are diagnostic scripts and always exit 0.

## Usage

```bash
./run_all.sh                 # run everything in sequence
./smoke4.sh                  # just the asserted proxy edge cases
./smoke_ws.sh                # just the WebSocket cases
SMOKE_OUT=/tmp/x ./smoke1.sh # custom scratch dir for logs/configs
```

Logs and generated configs land in `$SMOKE_OUT` (default `/tmp/aevrix-smoke`).
The mock upstream listens on ports 19000-19002/19006 and the server on
`127.0.0.1:18080`; scripts run sequentially so the ports are reused safely.
