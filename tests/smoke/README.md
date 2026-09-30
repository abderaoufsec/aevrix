# Reverse-proxy smoke tests (Phase 22)

Live end-to-end checks for the reverse-proxy feature. Each script starts a
Python mock upstream plus a local `aevrix` server, exercises it with `curl`,
prints the results, and tears both down.

## Prerequisites

- Server binary built at `../../build/aevrix` (from the repo root:
  `cmake -B build && cmake --build build -j`)
- `python3` and `curl` on PATH

## Scripts

| Script | Covers |
|---|---|
| `smoke1.sh` | prefix stripping, X-Forwarded-* headers, chunked re-framing, status passthrough, HEAD, static files, keep-alive |
| `smoke2.sh` | 504 on upstream stall, HEAD timing, socket state |
| `smoke3.sh` | HEAD on static vs proxied paths, GET after HEAD |
| `smoke4.sh` | asserted edge cases: oversize response (`max_response_bytes`), 1xx interim, 101 upgrade rejection, CL/TE smuggling rejection, truncated body, close-delimited body, HEAD bodylessness (success + error replies), sequential keep-alive |

`smoke4.sh` prints `RESULT: N passed, M failed` and exits non-zero on any
failure. `smoke1-3` are diagnostic scripts and always exit 0.

## Usage

```bash
./run_all.sh                 # run everything in sequence
./smoke4.sh                  # just the asserted edge cases
SMOKE_OUT=/tmp/x ./smoke1.sh # custom scratch dir for logs/configs
```

Logs and generated configs land in `$SMOKE_OUT` (default `/tmp/aevrix-smoke`).
The mock upstream listens on ports 19000-19002/19006 and the server on
`127.0.0.1:18080`; scripts run sequentially so the ports are reused safely.
