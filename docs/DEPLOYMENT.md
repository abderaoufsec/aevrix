# Aevrix Deployment Guide

How to run Aevrix v1.0 safely in production. Companion to
[README](../README.md) (quick start), [SECURITY](../SECURITY.md) (policy),
[TLS](TLS.md), [Observability](OBSERVABILITY.md), and
[Benchmarks](BENCHMARKS.md).

## 1. System Requirements

- **OS:** Linux x86_64 (primary; CI runs `ubuntu-latest`). Windows/MinGW
  builds exist but production validation is Linux-first.
- **CPU/RAM:** any modern 64-bit CPU; 512 MB RAM is comfortable for modest
  static workloads (budget more for large `max_connections` / buffers).
- **Disk:** small binary + `document_root` content + writable log dir.
- **Dependencies:** OpenSSL >= 1.1.1 for TLS builds (`libssl-dev` at build
  time, `libssl` at runtime); `curl`/`python3` only for smoke tests.
- **Network:** one TCP port for HTTP (`port`), one for TLS (`tls_port`)
  when enabled; loopback admin access (see section 4).

Build from a tagged release and verify checksums (see README release
section). Prefer the Release build (`-DCMAKE_BUILD_TYPE=Release`).

## 2. Deployment Topologies

Single event-loop process per instance — scale by running instances:

```text
clients -> firewall -> (optional front proxy: nginx/caddy) -> aevrix:8080
clients -> firewall -> aevrix:8443 (TLS, OpenSSL)
```

- **Behind a front proxy** (recommended for Internet-facing): terminate
  public TLS / rate limiting / WAF at the front proxy, forward to Aevrix
  over a private network. Treat inbound `X-Forwarded-*` as untrusted.
- **Direct:** bind `host` to the intended interface only (sample binds
  `127.0.0.1`; set the LAN IP or `0.0.0.0` deliberately), open only the
  needed ports.
- **Redundancy:** run N instances behind a load balancer; no shared state
  between instances (each has its own config generation, pools, metrics).

## 3. Production Configuration

Start from the shipped `aevrix.conf` and set every key deliberately.
Restart-required keys (`port`, `host`, `workers`, `document_root`,
`tls_*` identity, `proxy_pass`/`proxy_prefix`, `websocket_paths`) need a
```ini
host = 10.0.0.5
port = 8080
workers = 4
document_root = /srv/www
header_timeout_ms = 10000
body_timeout_ms = 30000
keep_alive_timeout_ms = 5000
write_timeout_ms = 30000
max_connections = 1000
max_buffer_size = 65536
max_request_body = 10485760
tls_enabled = true
tls_cert_file = /etc/aevrix/cert.pem
tls_key_file = /etc/aevrix/key.pem
tls_port = 8443
admin_api_enabled = false
log_level = info
```

Guidance: `workers` ~ CPU cores for file-heavy loads (event loop stays
non-blocking; workers handle blocking file work). Raise `max_connections`
only with matching fd limits (`ulimit -n`) and RAM. Keep header/body/write
timeouts tight; `keep_alive_timeout` ~5 s is sane. `max_request_body`
caps memory per request, `max_buffer_size` caps parse buffering, and
`proxy_max_response_bytes` caps buffered upstream bodies — tune to your
largest legitimate payload. Keep `document_root` read-only and served by a
dedicated non-root user. Reloads are all-or-nothing: SIGHUP never applies
a half-parsed file, and restart-required keys only warn in the log.

Optional subsystems (all disabled by default):

```ini
proxy_enabled = true
proxy_pass = http://127.0.0.1:9001
proxy_prefix = /api
proxy_strip_prefix = true
proxy_connect_timeout_ms = 2000
proxy_read_timeout_ms = 5000
proxy_max_idle_connections = 4
proxy_max_response_bytes = 4194304
proxy_idle_timeout_ms = 60000
websocket_enabled = true
websocket_paths = /ws
websocket_max_message_bytes = 1048576
websocket_close_timeout_ms = 5000
websocket_allowed_origins = https://app.example.com
```

## 4. Hardening Checklist

1. Run as a **non-root** dedicated user (`useradd -r aevrix`), no shell.
2. Restrict the **admin API**: leave `admin_api_enabled = false` unless
   needed; if enabled, set a long random `admin_token`, bind to loopback
   or a management interface, and firewall `/admin/*`.
3. **TLS key** at `0600`, owned by the service user (or root with group
   read); real CA cert in production — never the smoke-test self-signed.
4. Filesystem: read-only `document_root`, no symlinks escaping it.
5. OS: firewall default-deny inbound except your ports, keep OpenSSL and
   the kernel patched, `ulimit -n` sized to `max_connections`.
6. Secrets hygiene: tokens/keys only in `aevrix.conf` (mode `0600`) or a
   secret manager — never in logs, URLs, or the repo.

A suitable `systemd` unit runs the binary with `--config`, restarts on
failure, and confines the process:

```ini
[Unit]
Description=Aevrix HTTP server
After=network.target

[Service]
User=aevrix
Group=aevrix
ExecStart=/usr/local/bin/aevrix --config /etc/aevrix/aevrix.conf
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
LimitNOFILE=4096

[Install]
WantedBy=multi-user.target
```

`systemctl reload aevrix` then performs a hot SIGHUP reload.

## 5. Monitoring

- **`GET /health`** — liveness probe (load-balancer health check).
- **`GET /metrics`** — Prometheus text: request/connection/error counts,
  latency percentiles, bytes, parser failures. Scrape every 15-30 s and
  alert on 5xx rate, p95 latency, connection saturation, error spikes.
  Field catalogue: [OBSERVABILITY](OBSERVABILITY.md).
- **Logs** — structured, levelled (`log_level = info` default; `debug`
  only for triage), carrying connection/request IDs. Ship to your
  aggregator, rotate (`logrotate`), alert on `level=error` bursts.
- **Smoke after deploy** — run `bash tests/smoke/run_all.sh` against a
  staging instance before promoting.

## 6. Scaling

- Aevrix is a **single event loop + bounded workers** per process:
  vertical scaling means more instances behind a balancer, not flags.
- Keep pooled subsystems bounded: `proxy_max_idle_connections`,
  `proxy_idle_timeout_ms`, `websocket_max_message_bytes` — raise only with
  measured need; each idle upstream socket and large frame budget costs
  file descriptors and RAM.
- Front-cache static assets at a CDN or front proxy; Aevrix already emits
  ETag/Last-Modified and honours conditional + Range requests.
- Capacity-test with the `benchmarks/` harness **and** realistic traffic;
  micro-benchmarks are regression signals, not capacity promises (see
  [BENCHMARKS](BENCHMARKS.md)).

## 7. Upgrades and Rollback

1. Deploy the new tagged binary + config side by side; diff the config
   (`ServerConfigStore::describe` log line lists applied keys).
2. Canary one instance; watch `/metrics`, `/health`, and logs for one
   scrape interval.
3. Roll forward through the balancer; rollback is the previous tag +
   previous config (restart-required keys need a restart — plan the flap).
4. Record version/commit/compiler/build-mode per release (release notes
   carry these; `BUILD-INFO.txt` ships with artifacts).

## 8. Troubleshooting

| Symptom | Likely cause — check |
|---|---|
| Bind fails / `address in use` | stale process or another service on `port`/`tls_port`; `ss -ltnp`. |
| TLS handshake errors | wrong cert/key paths, key mismatch, client below TLS 1.2, expired cert; see [TLS](TLS.md#troubleshooting). |
| 502/504 on `/api` paths | upstream down / slow; check `proxy_pass`, connect/read timeouts, upstream logs. |
| 413 / closed uploads | `max_request_body` / `max_buffer_size` too small for the payload. |
| Slow-client stalls | tighten `header/body/write_timeout_ms`; confirm front-proxy timeouts. |
| Reload logged but nothing changed | key is restart-required (port/host/workers/paths) — restart. |
| Reload rejected | invalid candidate config — fix the file; live generation untouched. |
| 401 on `/admin` paths | missing/wrong `Authorization: Bearer` or `X-Aevrix-Token`. |

When asking for help, include version/commit, redacted config, and the
relevant log slice — see [CONTRIBUTING](../CONTRIBUTING.md).


process restart; everything else hot-reloads via SIGHUP or
`POST /admin/reload-config`.
