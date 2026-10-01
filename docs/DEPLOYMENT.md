# Deployment

What Aevrix needs, how to turn TLS on, how to run it under systemd, and what
to check when it misbehaves. Keys are in [CONFIGURATION.md](CONFIGURATION.md);
the security position is in [SECURITY.md](../SECURITY.md).

## Requirements

Linux is the supported platform (epoll, SIGHUP reload); a blocking fallback
exists for other POSIX systems and for Windows, untested. You need CMake
3.20 or newer, a C++20 compiler (GCC 11+ or Clang 14+), a threads library and,
for a TLS build, OpenSSL development files (`libssl-dev`); without OpenSSL,
CMake disables TLS and the `tls_*` keys are ignored. An instance binds one
HTTP port and, with TLS, a second one, both on `host`; the proxy accepts
`http://` upstreams only. Build with `-DCMAKE_BUILD_TYPE=Release` from a tag
and verify the published checksum.

## Hardening

Run as an unprivileged user with its own group: read-only `document_root`, no
write access elsewhere, `0600` on the config file (it can hold `admin_token`)
and on `tls_key_file`. Leave `admin_api_enabled = false` unless you need the
admin routes; with it on, a long random token plus a loopback or
management-network bind is all that protects a reload. Do not publish the
plain-HTTP port; set the timeouts and `max_connections` deliberately.

## TLS

TLS terminates in Aevrix on its own listener. SSLv3, TLS 1.0 and TLS 1.1 are
disabled and compression is off; `tls_min_version` and `tls_max_version` take
`TLSv1.2` or `TLSv1.3`. One certificate is loaded at startup: no SNI
multi-cert, no client-certificate authentication, no OCSP stapling, no
resumption tuning. The handshake shares the `header_timeout_ms` budget. Test
certificate: `openssl req -x509 -newkey rsa:2048 -nodes -days 30 -keyout
key.pem -out cert.pem -subj /CN=host`.

```ini
tls_enabled = true
tls_cert_file = /etc/aevrix/cert.pem
tls_key_file = /etc/aevrix/key.pem
tls_min_version = TLSv1.2
tls_max_version = TLSv1.3
tls_port = 8443
```

Unreadable files, a mismatched certificate/key pair or `tls_port = 0` abort startup with the OpenSSL error in the log. Terminate TLS at a front proxy if you need public traffic or more certificates.

## systemd

```ini
[Unit]
Description=Aevrix HTTP server
After=network.target

[Service]
User=aevrix
WorkingDirectory=/srv/www
ExecStart=/usr/local/bin/aevrix --config /etc/aevrix/aevrix.conf
ExecReload=/bin/kill -HUP $MAINPID
StandardOutput=append:/var/log/aevrix/aevrix.log
Restart=on-failure
NoNewPrivileges=true
ProtectSystem=strict

[Install]
WantedBy=multi-user.target
```

`WorkingDirectory` matters because a relative `document_root` resolves against it. `systemctl reload aevrix` sends SIGHUP: hot keys apply immediately, while restart-only keys (`host`, `port`, `workers`, the `tls_*` identity, `proxy_enabled`/`proxy_pass`/`proxy_prefix`) are logged as needing a restart.

## Behind a reverse proxy

Forward HTTP/1.1 to `port` or `tls_port`. Aevrix appends the peer address to
`X-Forwarded-For` and sets `X-Forwarded-Proto` and `X-Forwarded-Host`, so strip
client-supplied copies at the edge and do not forward `/admin/*` from the
Internet. As the reverse proxy itself, Aevrix answers `502` on failure, `504` on a slow upstream and `503` on an empty pool, and `proxy_max_response_bytes`, `proxy_max_idle_connections` and `proxy_idle_timeout_ms` bound its buffers and pool.

## Monitoring

`GET /health` is the only endpoint reporting state; `/metrics` returns a placeholder, so watch `/health`, the log stream and the process. Log format: [OBSERVABILITY.md](OBSERVABILITY.md). Smoke-test staging with `bash tests/smoke/run_all.sh` before promoting.

## Troubleshooting

| Symptom | Check |
|---|---|
| `address in use` | another process on `port`/`tls_port`; `ss -ltnp` |
| `Document root does not exist` | relative paths resolve against the working directory; use an absolute path plus `WorkingDirectory=` |
| TLS handshake fails | paths, key mismatch, client below TLS 1.2, expired certificate |
| 502/504 on proxied paths | `proxy_pass`, upstream reachability, proxy connect/read timeouts |
| 404 for an existing file | it is a directory (403), a proxy prefix matched, or a query string changed the target |
| No response at all | unparsable request line or header, or a body over the parser limit |
| "needs a restart" or "rejected" | a restart-only key changed, or the candidate config failed validation |
| 401 from `/admin/*` | the bearer token does not match `admin_token` |
