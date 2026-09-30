# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |
| < 1.0   | :x: (pre-release, educational snapshots only) |

Only the latest v1.0.x patch release is supported. Pre-1.0 commits and tags
are development history and must not be deployed.

## Reporting a Vulnerability

**Do not open a public issue for a suspected vulnerability.**

- Email the maintainers via a private channel: open a
  [GitHub Security Advisory](https://github.com/abderaoufsec/aevrix/security/advisories/new)
  (preferred), or contact the repository owner directly through GitHub.
- Include: affected version / commit SHA, configuration (`aevrix.conf`
  redacted — strip `admin_token`, TLS key paths), reproduction steps or PoC,
  observed vs expected behaviour, and any logs (strip secrets).
- You will receive an acknowledgement within 72 hours.
- We will coordinate a fix and a disclosure timeline with you before any
  public release. Please do not disclose publicly until we have shipped a fix
  and agreed a date (typical target: 90 days or less).

### What happens next

1. Triage and reproduction.
2. Fix on a private branch, backported to the supported line if needed.
3. `SECURITY` entry + patched release + advisory publication.
4. Credit to the reporter (unless anonymity is requested).

## Scope

In scope: the Aevrix server binary and its HTTP/TLS/proxy/WebSocket/config
stack (`src/`, `include/aevrix/`), the sample `aevrix.conf`, and the build
(`CMakeLists.txt`, presets) where a default leads to an unsafe deployment.

Out of scope: third-party dependencies (OpenSSL, compilers, OS), the
benchmark harness as a performance oracle, and test-only credentials under
`tests/`.

## Security Properties (v1.0)

What v1.0 enforces — and what operators must still get right:

- **Transport:** TLS 1.2 minimum via OpenSSL; SSLv2/SSLv3/TLS 1.0/1.1
  disabled. Plain HTTP remains available only where explicitly configured —
  do not expose it for sensitive traffic. See `docs/TLS.md`.
- **HTTP framing:** strict RFC 9112 parsing; `Content-Length` /
  `Transfer-Encoding` conflicts and request-smuggling shapes are rejected
  (400/501), never guessed. See `docs/HTTP_CORRECTNESS.md`.
- **Path traversal:** document-root confinement via `validate_path()`; `..`,
  encoded variants, absolute and Windows paths rejected. See `TESTING.md`
  (path security) — but keep directory permissions tight regardless.
- **Resource limits:** every deployment must set `max_connections`,
  `max_buffer_size`, `max_request_body`, header/body/keep-alive/write
  timeouts. Defaults are starting points, not capacity planning.
- **Proxy:** upstream pooling with timeouts; failures map to 502/504; hop
  headers (`X-Forwarded-*`) are set, not trusted blindly. Validate any
  upstream you forward to. See `docs/ARCHITECTURE.md`.
- **WebSocket:** RFC 6455 handshake validation (key/accept, version 13,
  Connection token), client masking enforced, origin/path allowlists,
  UTF-8 + message-size limits, Ping/Pong/Close policy. See Phase 23 notes.
- **Admin surface:** `GET /admin/config` and `POST /admin/reload-config`
  are opt-in (`admin_api_enabled`) and token-protected (constant-time
  compare, token never logged, masked in responses). Bind admin to
  loopback or a management network; `?path=` is same-directory guarded.
- **Logging:** structured logs carry connection/request IDs. Do not log
  tokens, keys, or request bodies; rotate logs and restrict file
  permissions.

## Operator Best Practices

1. Run the latest v1.0.x; subscribe to GitHub releases / advisories.
2. Run as a non-root user, least privilege, read-only document root,
   writable log dir only.
3. Prefer TLS everywhere; use a real CA cert in production (repo ships no
   credentials), strong file permissions on `tls_key_file` (`0600`).
4. Set a long random `admin_token` (or disable the admin API), restrict
   `/admin/*` at the network layer too.
5. Put sane limits/timeouts in `aevrix.conf`, front with a hardened reverse
   proxy / firewall, and monitor `/metrics` + `/health` (see
   `docs/DEPLOYMENT.md`, `docs/OBSERVABILITY.md`).
6. Build from tagged releases, verify checksums, and keep OpenSSL patched.

## Known Limitations (accepted, documented)

- Single event-loop process; no privilege separation / sandboxing.
- No client-certificate (mTLS), OCSP stapling, session-resumption tuning,
  or SNI multi-cert support yet (see `docs/TLS.md` limitations).
- No built-in rate limiting or WAF semantics — use a front proxy.
- Benchmarks are micro-benchmarks, not security or capacity claims.

Thank you for helping keep Aevrix and its users safe.
