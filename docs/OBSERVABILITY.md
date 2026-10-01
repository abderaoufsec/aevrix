# Observability

## Endpoints

| Route | Response | Auth |
|---|---|---|
| `GET /health` | 200 `text/plain`, body `OK\n` | none |
| `GET /metrics` | 200 `text/plain`, body `Metrics endpoint - not yet implemented\n` | none |
| `GET /admin/config` | 200 status text, 404 when `admin_api_enabled = false` | `admin_token` when set |
| `POST /admin/reload-config` | 200 or 400 reload result, 404 when disabled | `admin_token` when set |

`GET /admin/config` reports `generation=`, `config_path=`, `reload_successes=`,
`reload_failures=` and `last_error=` after a rejection. `POST
/admin/reload-config` reloads the configured file or the file named by
`?path=`, a `path=<file>` body or a bare body path; an override must be
absolute, inside the configured file's directory and free of `..`. Success
answers 200 with `reload: applied`, `generation=`, `config_path=`, `changed=`,
`applied_now=` and `restart_required=`; rejection answers 400 with
`reload: rejected`, `generation=` and `error=`. Both carry
`X-Aevrix-Config-Generation`.

Authenticate with `Authorization: Bearer <token>` or `X-Aevrix-Token:`
`<token>`; a missing or wrong token answers 401 with `WWW-Authenticate: Bearer
realm="aevrix-admin"`. An empty `admin_token` disables the check, so keep the
admin surface on loopback or a management network.

## Metrics

`include/aevrix/metrics.h` defines counters and an exporter for
`aevrix_requests_total`, `aevrix_requests_by_status`, `aevrix_connections_total`,
`aevrix_connections_active`, `aevrix_errors_total`, `aevrix_latency_p50/p95/p99/mean`,
`aevrix_bytes_sent_total`, `aevrix_bytes_received_total`, `aevrix_parser_failures_total`.
No code path increments them and `/metrics` is not connected to the exporter,
so a scrape returns the placeholder line and no series.

```yaml
scrape_configs:
  - job_name: aevrix
    metrics_path: /metrics
    static_configs: [{ targets: ["127.0.0.1:8080"] }]
```

## Logs

Structured lines go to stdout as `2026-10-01 06:28:10.937 INFO conn=1 req=1
Routed GET /metrics`: local time with milliseconds, then `debug`, `info`,
`warn` or `error`, then `conn=` and `req=` where known. `log_level` sets the
threshold and reloads hot. Older code paths still write bare `std::cout` lines
such as `Starting TCP listener on 127.0.0.1:8080`. Tokens, keys and bodies are
never logged.
