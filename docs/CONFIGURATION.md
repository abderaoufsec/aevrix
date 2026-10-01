# Configuration

Aevrix reads one flat `key = value` file. Pass it with `--config FILE`; without
that flag the built-in defaults apply. Blank lines and lines starting with `#`
are ignored, whitespace around keys and values is trimmed, and a line without
`=` is skipped with a warning. Unknown keys are stored and then ignored.

`--root PATH` overrides `document_root` for that run. Values are read once at
startup; SIGHUP and `POST /admin/reload-config` re-read the same file (see the
last column: `restart` means the change is logged but needs a process restart,
`hot` means it takes effect immediately).

| Key | Default | Meaning | Reload |
|---|---|---|---|
| `host` | `127.0.0.1` | Listen address for both listeners | restart |
| `port` | `8080` | Plain HTTP port | restart |
| `workers` | `4` | Worker threads for filesystem work | restart |
| `document_root` | `./public` | Static file root, resolved against the working directory | hot |
| `header_timeout_ms` | `10000` | Budget for reading request headers (also the TLS handshake) | hot |
| `body_timeout_ms` | `30000` | Budget for reading a request body | hot |
| `keep_alive_timeout_ms` | `5000` | Idle budget between requests on a kept-alive connection | hot |
| `write_timeout_ms` | `30000` | Budget for flushing a response | hot |
| `max_connections` | `1000` | Accepted connections tracked at once | hot |
| `max_buffer_size` | `65536` | Per-connection read buffer ceiling | hot |
| `max_request_body` | `10485760` | Largest accepted `Content-Length` | hot |
| `log_level` | `info` | `debug`, `info`, `warn` or `error` | hot |
| `tls_enabled` | `false` | Start a second, TLS listener | restart |
| `tls_port` | `443` | TLS port (same `host`) | restart |
| `tls_cert_file` | empty | PEM certificate chain | restart |
| `tls_key_file` | empty | PEM private key | restart |
| `tls_min_version` | `TLSv1.2` | `TLSv1.2` or `TLSv1.3` | restart |
| `tls_max_version` | `TLSv1.3` | `TLSv1.2` or `TLSv1.3` | restart |
| `proxy_enabled` | `false` | Forward matching requests upstream | restart |
| `proxy_pass` | empty | Upstream as `http://host[:port]` (scheme required, `https://` rejected) | restart |
| `proxy_prefix` | `/proxy` | Path prefix routed to the upstream | restart |
| `proxy_strip_prefix` | `true` | Remove the prefix before forwarding | hot |
| `proxy_connect_timeout_ms` | `5000` | Upstream connect budget | hot |
| `proxy_read_timeout_ms` | `30000` | Upstream response budget | hot |
| `proxy_max_idle_connections` | `4` | Pooled idle sockets kept per upstream | hot |
| `proxy_max_response_bytes` | `4194304` | Buffered upstream body ceiling | hot |
| `proxy_idle_timeout_ms` | `60000` | Idle budget for a pooled upstream socket | hot |
| `websocket_enabled` | `false` | Accept RFC 6455 upgrades | hot |
| `websocket_paths` | `/ws` | Comma-separated upgrade paths | hot |
| `websocket_max_message_bytes` | `1048576` | Declared frame payload ceiling | hot |
| `websocket_close_timeout_ms` | `5000` | Close-handshake budget | hot |
| `websocket_ping_interval_ms` | `0` | Server ping interval, `0` disables pings | hot |
| `websocket_allowed_origins` | empty | Exact `Origin` allowlist, empty accepts any | hot |
| `admin_api_enabled` | `false` | Serve `/admin/config` and `/admin/reload-config` | hot |
| `admin_token` | empty | Bearer token for the admin routes, empty disables the check | hot |

Validation rejects `port = 0`, `workers = 0`, zero timeouts and zero limits,
and, when the dependent feature is enabled, a missing `proxy_pass`, a
non-absolute `proxy_prefix`, empty `websocket_paths`, non-absolute WebSocket
paths, an unknown `log_level`, and TLS without `tls_cert_file`/`tls_key_file`.
A rejected file leaves the running configuration untouched.

In a build without TLS (`-DENABLE_TLS=OFF` or no OpenSSL), the `tls_*` keys
are ignored.

`max_connections` and the timeouts are read from the live snapshot. Two
limits are not wired into the parser: the parser uses its own fixed defaults
(8 KiB request line, 8 KiB per header field, 100 headers, 64 KiB of headers,
1 MiB body), so `max_request_body` and `max_buffer_size` currently do not
change how much a single request may carry. Set `max_connections` and the
timeouts accordingly, and keep an external limit on request size.
