# Architecture

Aevrix is one process: a single event-loop thread that owns every socket, one
worker pool that only reads files, and components that keep those two apart.
This file describes what exists in the tree today.

```text
        accept            epoll                per-connection state
clients ------> TcpListener --> EventLoop --> Connection (parser, buffers,
                                     |          deadlines, TLS, WebSocket)
                                     |
                                     +--> Router        (method + target)
                                     +--> WebSocket     (upgrade + frames)
                                     +--> ProxyHandler  (pooled upstreams)
                                     +--> WorkerPool    (file reads)
                                              |
                                     completion eventfd (results back to loop)
```

## Components

- `TcpListener` (`tcp_listener.{h,cpp}`): socket, `SO_REUSEADDR`, bind, listen
  with backlog 128, blocking accept at startup then non-blocking accept from
  the loop. A second instance serves `tls_port` when TLS is enabled.
- `EventLoop` (`event_loop.{h,cpp}`): epoll wrapper. `add_fd(fd, events,
  callback)`, `modify_fd`, `remove_fd`, and `run(timeout_ms)`, which the main
  loop calls with a 1 s tick.
- `ConnectionManager` (`connection_manager.{h,cpp}`): `fd -> Connection` map,
  lookup by connection id, `sweep_timeouts()`, WebSocket maintenance and
  shutdown fan-out, and the `max_connections` check at accept time.
- `Connection` (`connection.{h,cpp}`): per-connection state: sockets, input
  buffer, parser, output buffer, read/write state, deadline, keep-alive flag,
  optional `TlsConnection` and `WebSocketConnection`.
- `HttpRequestParser` (`http_request_parser.{h,cpp}`): incremental request-line,
  header and body parsing with its own fixed limits.
- `Router` (`router.{h,cpp}`): `(METHOD, target) -> handler` map, exact match,
  case-insensitive method.
- `StaticFileServer` (`static_file_server.{h,cpp}`): URL decode, resolve, path
  containment check, MIME lookup by extension, file read.
- `WorkerPool` (`worker_pool.h`), `FilesystemWorker` (`filesystem_worker.cpp`),
  `WorkerCompletionHandler` (`worker_completion_handler.{h,cpp}`): bounded
  queue (128 jobs), `workers` threads, results returned over an eventfd.
- `ProxyHandler` (`proxy_handler.{h,cpp}`), `UpstreamPool` (`upstream_pool.*`),
  `ProxyTarget` (`proxy_target.*`), `ProxyRequestBuilder`
  (`proxy_request_builder.*`), `ProxyResponseParser`
  (`proxy_response_parser.*`): forwarding, connection reuse, framing checks.
- `WebSocketHandshake` (`websocket_handshake.*`) and `WebSocketFrame`,
  `WebSocketConnection` (`websocket_*.cpp`): upgrade validation and the frame
  layer that takes over the connection after 101.
- `ServerConfig`, `ConfigParser`, `ServerConfigStore`: configuration and the
  atomic generation store described below.
- `Logger` (structured lines), `SignalHandler` (SIGINT/SIGTERM/SIGHUP),
  `Metrics`/`observability` (present, not wired into the server).

## Threading

The event-loop thread runs `main`: accepts, reads, writes, parses, routes,
talks to upstreams, TLS handshakes, WebSocket frames, timeouts and configuration
reloads all happen there, and nothing in that path blocks.

Blocking work is the file read behind static serving. A read event that reaches
the static path copies what the worker needs (connection id, target, document
root, HEAD flag) into a `WorkerTask`, marks the connection worker-active (which
suspends its deadline) and submits the job, then returns to epoll immediately. A
pooled thread runs `StaticFileServer::serve_file` on its own copy of the task,
pushes the result to a completion queue and writes to an eventfd the loop
watches. The loop looks the connection up by id, clears the worker flag,
refreshes the deadline and writes the response. Workers never see a socket, a
`Connection`, TLS state or WebSocket state.

Signals are handled the same way: the handler sets a flag and writes to an
eventfd, and the loop performs the work. SIGHUP means reload, SIGINT/SIGTERM
mean graceful shutdown (stop accepting, notify WebSocket peers, drain, stop
workers, exit).

## Request dispatch

For a complete request the loop tries, in order, and stops at the first match:

1. `Router` — exact `(method, target)` match, so a query string misses the
route.
2. WebSocket upgrade — only when `websocket_enabled` and the request's path
component is in `websocket_paths`. An upgrade attempt on an allowlisted path
that fails validation is answered with 400, 403 or 426 and the connection is
closed; a valid one answers 101 and switches the connection to frame mode.
3. `ProxyHandler` — when `proxy_enabled` and the path component matches
`proxy_prefix`.
4. `WorkerPool` — everything else: static file from `document_root`.

Errors along the way close the connection rather than leaving a half-written
exchange: an unparsable request, a serialization failure, an fs error from the
worker, or a proxy exchange that produced no client response.

## Connection lifecycle

`ConnectionState` moves `New -> (TlsHandshake) -> Reading -> Writing ->
Waiting -> Closing/Closed`. An upgraded connection leaves that ladder and stays
in frame mode until the close handshake completes.

The timeout budget comes from the state and is reset whenever bytes move:
header budget while reading headers (and during the TLS handshake), body budget
while reading a body, write budget while flushing, keep-alive budget while
idle, WebSocket close budget during a close handshake. A connection with a
worker job in flight has no deadline until the result arrives. The loop sweeps
`has_deadline_exceeded()` once per second and drops expired connections.

## Configuration reload

`ServerConfigStore` holds `shared_ptr<const ServerConfig>`. Every event takes a
snapshot; `publish()` swaps the pointer and increments a generation counter, so
a request that started under generation N keeps seeing N while a reload lands.

`try_reload()` parses and validates a candidate file. A rejection leaves the
active generation untouched and records the reason (`/admin/config` reports it).
On success the changed keys are classified: `host`, `port`, `workers`, the
`tls_*` identity, and `proxy_enabled`/`proxy_pass`/`proxy_prefix` need a restart
and only get a warning, everything else is logged as applied. Two values are
pushed because the objects holding them outlive a snapshot: the log level, and
the upstream pool's idle/timeout bounds. The rest are pulled per event from the
snapshot, including `document_root` and all `websocket_*` settings.

SIGHUP, `POST /admin/reload-config` and the 1 s maintenance tick all call the
same reload entry point on the loop thread, so a reload never races with the
request path.

## Invariants

- One owner per descriptor. `UniqueFd` and `Connection` close it; a descriptor
  is removed from epoll before the owner is destroyed.
- Every socket handled by the loop is non-blocking. Partial reads and writes
  keep the remaining bytes in the buffers and re-arm the interest mask.
- Only the loop thread touches `Connection`, TLS and WebSocket objects.
- Configuration is read as an immutable snapshot; no lock is held across I/O,
  and a rejected reload never publishes a partial file.
- Static paths are decoded, resolved and confirmed to stay inside
  `document_root` before the file is opened, so `..`, encoded variants,
  absolute paths and symlinks that leave the root are refused.
- Proxy framing is regenerated rather than forwarded: hop-by-hop headers are
  stripped in both directions, `Content-Length` is recomputed for the client,
  and an upstream response that mixes `Content-Length` with
  `Transfer-Encoding`, malformed chunked framing, a body over
  `proxy_max_response_bytes` or a truncated body is treated as a failure.
- WebSocket frames are validated before allocation: RSV bits and reserved
  opcodes are rejected, client frames must be masked, control frames must be
  short and unfragmented, and a declared payload over
  `websocket_max_message_bytes` is refused.
- The admin routes stay invisible (404) unless `admin_api_enabled` is true, and
  a configured token is compared without early exit.
