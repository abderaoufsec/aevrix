# Security policy

Aevrix is an educational server, not a hardened one: no audit, no fuzzing, and
several protocol edges are more permissive than the RFCs ([HTTP.md](docs/HTTP.md)).
Run it behind a front proxy or on a trusted network.

## Supported versions

Version 1.0.x is supported (latest patch only); older versions are development snapshots.

## Reporting a vulnerability

Do not open a public issue. Open a [GitHub Security Advisory](https://github.com/abderaoufsec/aevrix/security/advisories/new)
with the version or commit SHA, build flags, a redacted `aevrix.conf` and a
minimal proof of concept.

Acknowledgement within 72 hours; fixes ship coordinated with you, targeting 90
days or less. Please hold off on public disclosure until then.

## Scope

In scope: the server binary and its HTTP, TLS, proxy, WebSocket, configuration
and admin code, the sample `aevrix.conf`, and build defaults that invite unsafe
deployments. Out of scope: OpenSSL, the compiler and the OS, benchmarks.

## Design limitations

- Request framing is lenient: header-name whitespace, duplicate
  `Content-Length` and a non-digit suffix are accepted, and `Transfer-Encoding`
  is ignored.
- Parser limits are compiled in: `max_request_body` and `max_buffer_size` do
  not change what a request may carry.
- Any parsed method that matches no route is served as a file, so there is no
  405 path.
- TLS serves one certificate (no SNI selection, client certificates, stapling
  or resumption), plain HTTP stays open on its own port, and proxy upstreams
  must be `http://`.
- No rate limiting, privilege separation, sandboxing or clustering, and
  `/metrics` is a placeholder.

## Operator notes

Run as a non-root user with a read-only document root and `0600` on the config file
and TLS key. Keep `admin_api_enabled = false` unless you use it, and bind to
loopback.
