# TLS/HTTPS Support (Phase 21)

## Overview

Aevrix supports TLS/HTTPS using OpenSSL as a transport layer around the existing event-driven architecture. TLS is integrated as a nonblocking, event-driven component that preserves the existing HTTP parser, router, WorkerPool, and timeout infrastructure.

## Architecture

### TLS Integration Flow

```
TCP accept
    ↓
nonblocking socket
    ↓
ConnectionManager
    ↓
TLS handshake (nonblocking, SSL_accept)
    ↓
HTTP parser (SSL_read provides plaintext)
    ↓
request dispatch
    ↓
WorkerPool when blocking work is required
    ↓
response/output buffer
    ↓
TLS write (SSL_write encrypts response)
    ↓
encrypted socket
```

### Key Design Principles

1. **TLS as a transport layer:** TLS wraps the existing connection I/O without redesigning the HTTP layer
2. **Nonblocking only:** All TLS operations (handshake, read, write, shutdown) are nonblocking
3. **WANT_READ/WANT_WRITE handling:** TLS can invert normal I/O expectations (read may need write, write may need read)
4. **RAII ownership:** `SSL_CTX` is owned by `TlsContext`, `SSL` is owned by `TlsConnection`
5. **Event-loop thread authority:** TLS state is accessed only on the event-loop thread
6. **WorkerPool isolation:** Worker threads never perform TLS or socket I/O

## Components

### TlsContext (`include/aevrix/tls_context.h`)

RAII wrapper for OpenSSL `SSL_CTX`. Responsible for:
- Creating and freeing `SSL_CTX`
- Loading certificate and private key
- Validating certificate/key match
- Configuring protocol versions (minimum TLS 1.2)
- Disabling insecure protocols (SSLv2, SSLv3, TLSv1, TLSv1.1)
- Setting secure defaults (no compression)

### TlsConnection (`include/aevrix/tls_connection.h`)

RAII wrapper for per-connection `SSL` objects. Responsible for:
- Creating `SSL` from `TlsContext`
- Associating SSL with socket file descriptor
- Nonblocking handshake (`SSL_accept`)
- Nonblocking read (`SSL_read`)
- Nonblocking write (`SSL_write`)
- Nonblocking shutdown (`SSL_shutdown`)
- Tracking handshake state
- Communicating I/O requirements to the event loop

### I/O Requirements

TLS operations return `TlsIoRequirement` to indicate what the event loop should wait for:

- `None`: No I/O required
- `WantRead`: Connection needs `EPOLLIN`
- `WantWrite`: Connection needs `EPOLLOUT`
- `Both`: Connection needs both `EPOLLIN` and `EPOLLOUT`
- `Closed`: Connection is closed

## Configuration

TLS configuration is integrated into `ServerConfig`:

| Configuration Key | Type | Description | Default |
|-------------------|------|-------------|---------|
| `tls_enabled` | boolean | Enable TLS/HTTPS | `false` |
| `tls_cert_path` | string | Path to certificate file (PEM) | (required if enabled) |
| `tls_key_path` | string | Path to private key file (PEM) | (required if enabled) |
| `tls_min_version` | string | Minimum TLS version | `TLSv1.2` |
| `tls_max_version` | string | Maximum TLS version | `TLSv1.3` |
| `tls_port` | integer | TLS listen port | `8443` |
| `tls_handshake_timeout` | integer | Handshake timeout (seconds) | `10` |

### Example Configuration

```
tls_enabled = true
tls_cert_path = /etc/aevrix/cert.pem
tls_key_path = /etc/aevrix/key.pem
tls_min_version = TLSv1.2
tls_max_version = TLSv1.3
tls_port = 8443
tls_handshake_timeout = 10
```

## Security Properties

### Protocol Security

- **Disabled protocols:** SSLv2, SSLv3, TLSv1.0, TLSv1.1
- **Minimum version:** TLSv1.2
- **Maximum version:** TLSv1.3
- **Compression:** Disabled to prevent CRIME attack

### Certificate Validation

- Certificate must exist and be readable
- Private key must exist and be readable
- Certificate and private key must match
- Invalid configuration fails server startup

### Handshake Security

- Nonblocking handshake prevents DoS via blocking
- Handshake timeout prevents slow-loris attacks
- Failed handshakes close only the affected connection
- Malformed ClientHello does not crash the server

### Runtime Security

- TLS does not bypass static-file path security
- TLS does not bypass timeout enforcement
- TLS state is isolated per connection
- Worker threads cannot access TLS state

## Build Configuration

### CMake Options

```bash
# Enable TLS (requires OpenSSL)
cmake -DENABLE_TLS=ON -DCMAKE_BUILD_TYPE=Debug

# Disable TLS (builds without OpenSSL dependency)
cmake -DENABLE_TLS=OFF -DCMAKE_BUILD_TYPE=Debug
```

### Dependencies

**Debian/Kali:**
```bash
sudo apt install libssl-dev
```

**Ubuntu:**
```bash
sudo apt install libssl-dev
```

**Fedora/RHEL:**
```bash
sudo dnf install openssl-devel
```

**macOS:**
```bash
brew install openssl
```

## Testing

### Unit Tests

- `aevrix_tls_context_tests`: Tests for `TlsContext` creation, certificate loading, protocol configuration
- `aevrix_tls_connection_tests`: Tests for `TlsConnection` creation, handshake state, I/O operations
- `aevrix_tls_integration_tests`: Integration tests with real OpenSSL operations

### Running TLS Tests

```bash
# Build with TLS enabled
cmake -S . -B build -DENABLE_TLS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run TLS tests
ctest --test-dir build -R tls
```

### Manual Testing

Generate a self-signed certificate:
```bash
openssl genrsa -out key.pem 2048
openssl req -new -x509 -key key.pem -out cert.pem -days 365 -subj "/CN=localhost"
```

Start the server with TLS:
```bash
./aevrix --config aevrix.conf
```

Test with curl:
```bash
curl -k https://localhost:8443/
```

The `-k` flag bypasses certificate verification for self-signed certificates.

## Epoll Integration

TLS operations dynamically change epoll interest:

| Operation | SSL Error | Epoll Interest |
|-----------|-----------|-----------------|
| `SSL_accept` | `WANT_READ` | `EPOLLIN` |
| `SSL_accept` | `WANT_WRITE` | `EPOLLOUT` |
| `SSL_read` | `WANT_READ` | `EPOLLIN` |
| `SSL_read` | `WANT_WRITE` | `EPOLLOUT` |
| `SSL_write` | `WANT_READ` | `EPOLLIN` |
| `SSL_write` | `WANT_WRITE` | `EPOLLOUT` |

The event loop updates epoll interest based on the `TlsIoRequirement` returned by each TLS operation.

## State Machine

The connection state machine includes TLS-specific states:

```
Accepted
    ↓
TLS Handshake (nonblocking)
    ↓
TLS Established
    ↓
HTTP Reading
    ↓
HTTP Processing / WorkerPool
    ↓
HTTP Writing
    ↓
Keep-alive or Close
```

HTTP parsing does not begin until the TLS handshake is complete.

## Limitations

1. **TLSv1.2 minimum:** Older clients using TLSv1.0 or TLSv1.1 cannot connect
2. **No TLS session resumption:** Not yet implemented
3. **No OCSP stapling:** Not yet implemented
4. **No client certificate authentication:** Server-only authentication
5. **Single certificate:** Does not support SNI (Server Name Indication) for multiple certificates
6. **Test certificates only:** Repository contains no production credentials

## Troubleshooting

### "OpenSSL not found"

Install OpenSSL development packages for your platform.

### "Failed to load certificate"

Check that:
- Certificate file exists
- Certificate file is readable
- Certificate is in PEM format

### "Certificate and private key do not match"

Verify that the certificate and private key were generated together.

### Handshake timeout

Increase `tls_handshake_timeout` in configuration if clients are slow.

### WANT_READ/WANT_WRITE loops

Ensure the event loop correctly updates epoll interest based on `TlsIoRequirement`.

## Future Enhancements

- TLS session resumption for performance
- OCSP stapling for certificate validation
- Client certificate authentication (mTLS)
- SNI support for multiple certificates
- TLS 1.3 early data (0-RTT)
- Automatic certificate renewal (ACME/Let's Encrypt)
